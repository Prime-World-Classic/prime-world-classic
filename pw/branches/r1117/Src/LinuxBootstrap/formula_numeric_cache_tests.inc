/** Cache tests share the probe's checked-result helpers. */
bool SameCacheStats(const NumericFormulaCacheStats& a, const NumericFormulaCacheStats& b)
{
	return a.entries == b.entries && a.hits == b.hits && a.misses == b.misses;
}

bool EmptyCacheStats(const NumericFormulaCacheStats& stats)
{
	return stats.entries == 0 && stats.hits == 0 && stats.misses == 0;
}

void CheckCacheError(const NumericFormulaResult& result, Error expected, const char* label)
{
	Check(!result.Succeeded() && result.error == expected && std::isnan(result.value), label);
}

/** Reusing compiled code must refresh symbol snapshots and execute every selected function call. */
void CachedFreshBindings()
{
	ClearNumericFormulaCache();
	Check(NumericFormulaCacheCapacity == 256 && EmptyCacheStats(GetNumericFormulaCacheStats()),
		"cache capacity and explicit empty/reset contract");
	const std::string expression = "live + live + random(2,3) + random(4,5)";
	for (int pass = 0; pass < 3; ++pass)
	{
		int symbolCalls = 0, functionCalls = 0;
		const double live = pass * 11 - 2;
		const auto before = GetNumericFormulaCacheStats();
		const auto result = EvaluateNumericFormula(expression,
			[&, live](const std::string& name, double& value) {
				++symbolCalls; value = live; return name == "live";
			}, [&, live](const std::string& name, const double* args, std::size_t count, double& value) {
				++functionCalls;
				if (name != "random" || count != 2) return false;
				value = live + args[0] + args[1]; return true;
			});
		Check(result.Succeeded() && Close(result.value, float(4 * live + 14))
			&& symbolCalls == 1 && functionCalls == 2, "cached expression uses only this call's bindings");
		const auto after = GetNumericFormulaCacheStats();
		Check(after.entries > 0 && after.entries <= NumericFormulaCacheCapacity, "successful expression leaves bounded reusable code");
		if (pass) Check(after.hits > before.hits, "repeated successful expression hits compiled cache");
	}
	CheckCacheError(EvaluateNumericFormula(expression), Error::MissingResolver, "cache cannot supply a previous symbol resolver");
	CheckCacheError(EvaluateNumericFormula(expression,
		[](const std::string&, double& value) { value = 8; return true; }), Error::MissingResolver,
		"cache cannot supply a previous function resolver");
	ClearNumericFormulaCache();
	Check(EmptyCacheStats(GetNumericFormulaCacheStats()), "clear removes idle code and resets statistics");
}

/** Changing a branch on a cache hit must neither resolve nor execute the untaken branch. */
void CachedBranchChoices()
{
	ClearNumericFormulaCache();
	const std::string expression = "gate ? (random(live,2), live + live) : (other && random(3,4))";
	const std::array<std::pair<bool, bool>, 4> choices{{{true, false}, {false, false}, {false, true}, {true, false}}};
	for (std::size_t i = 0; i < choices.size(); ++i)
	{
		const bool gate = choices[i].first, other = choices[i].second;
		const double live = 7 + i;
		std::vector<std::string> symbols;
		std::vector<double> calls;
		const auto before = GetNumericFormulaCacheStats();
		const auto result = EvaluateNumericFormula(expression,
			[&](const std::string& name, double& value) {
				symbols.push_back(name);
				if (name == "gate") value = gate;
				else if (gate && name == "live") value = live;
				else if (!gate && name == "other") value = other;
				else throw std::runtime_error("untaken cached symbol");
				return true;
			}, [&](const std::string& name, const double* args, std::size_t count, double& value) {
				if (name != "random" || count != 2) return false;
				calls.push_back(args[0]); value = args[0] + args[1]; return true;
			});
		Check(result.Succeeded() && Close(result.value, gate ? float(2 * live) : float(other)),
			"cached lazy branch uses current condition");
		Check(symbols == (gate ? std::vector<std::string>{"gate", "live"} : std::vector<std::string>{"gate", "other"})
			&& calls == (gate ? std::vector<double>{live} : other ? std::vector<double>{3} : std::vector<double>{}),
			"cached lazy symbol/function callback trace excludes untaken branches");
		if (i) Check(GetNumericFormulaCacheStats().hits > before.hits, "branch changes reuse the same compiled expression");
	}
}

/** The same source may recursively evaluate while an outer parser and symbol snapshot are active. */
void CachedRecursiveBindings()
{
	ClearNumericFormulaCache();
	const std::string expression = "recur + tail + recur";
	for (int pass = 0; pass < 2; ++pass)
	{
		int depth = 3;
		std::array<int, 4> recurCalls{}, tailCalls{};
		NumericSymbolResolver resolver;
		resolver = [&](const std::string& name, double& value) {
			if (name == "tail") { ++tailCalls[depth]; value = 100 + depth; return true; }
			if (name != "recur") return false;
			++recurCalls[depth];
			if (!depth) { value = 1; return true; }
			--depth;
			const auto nested = EvaluateNumericFormula(expression, resolver);
			++depth; value = nested.value; return nested.Succeeded();
		};
		const auto result = EvaluateNumericFormula(expression, resolver);
		float expected = 102;
		for (int level = 1; level <= 3; ++level) expected = 2 * expected + 100 + level;
		Check(result.Succeeded() && result.value == expected && depth == 3,
			"same-expression symbol recursion preserves each active evaluation");
		Check(recurCalls == std::array<int, 4>{1, 1, 1, 1} && tailCalls == recurCalls,
			"recursive calls have separate once-per-symbol snapshots");
	}

	const std::string functionExpression = "value + random(1,2) + value";
	for (int pass = 0; pass < 2; ++pass)
	{
		int depth = 2, symbolCalls = 0, functionCalls = 0;
		const NumericSymbolResolver symbols = [&](const std::string& name, double& value) {
			++symbolCalls; value = depth + 1; return name == "value";
		};
		NumericFunctionResolver functions;
		functions = [&](const std::string& name, const double*, std::size_t count, double& value) {
			++functionCalls;
			if (name != "random" || count != 2) return false;
			if (!depth) { value = 3; return true; }
			--depth;
			const auto nested = EvaluateNumericFormula(functionExpression, symbols, functions);
			++depth; value = nested.value; return nested.Succeeded();
		};
		const auto result = EvaluateNumericFormula(functionExpression, symbols, functions);
		Check(result.Succeeded() && result.value == 15 && depth == 2 && symbolCalls == 3 && functionCalls == 3,
			"same-expression function recursion restores outer callback userdata and symbol values");
	}
}

/** More than capacity distinct formulas and a clear may occur while a cached parser is leased. */
void CachedActiveEvictionAndClear()
{
	ClearNumericFormulaCache();
	const std::string expression = "anchor + random(1,2) + anchor + tail";
	const NumericSymbolResolver symbols = [](const std::string& name, double& value) {
		value = name == "anchor" ? 7 : 11; return name == "anchor" || name == "tail";
	};
	const NumericFunctionResolver functions = [](const std::string&, const double*, std::size_t, double& value) {
		value = 3; return true;
	};
	const auto warm = EvaluateNumericFormula(expression, symbols, functions);
	Check(warm.Succeeded() && warm.value == 28, "active-eviction expression is compiled before leasing");
	int anchorCalls = 0, tailCalls = 0, functionCalls = 0;
	const auto outer = EvaluateNumericFormula(expression,
		[&](const std::string& name, double& value) {
			if (name == "anchor") ++anchorCalls;
			if (name == "tail") ++tailCalls;
			return symbols(name, value);
		}, [&](const std::string&, const double*, std::size_t, double& value) {
			++functionCalls;
			std::string last;
			for (std::size_t i = 0; i < NumericFormulaCacheCapacity + 8; ++i)
			{
				last = "offset + " + std::to_string(i);
				ExpectValue(last, float(1000 + i), [](const std::string& name, double& output) {
					output = 1000; return name == "offset";
				});
				Check(GetNumericFormulaCacheStats().entries <= NumericFormulaCacheCapacity,
					"nested insertion never exceeds idle cache capacity");
			}
			const auto before = GetNumericFormulaCacheStats();
			ExpectValue(last, float(1000 + NumericFormulaCacheCapacity + 7),
				[](const std::string&, double& output) { output = 1000; return true; });
			Check(GetNumericFormulaCacheStats().hits > before.hits, "reuse still works after capacity pressure");
			ClearNumericFormulaCache();
			Check(EmptyCacheStats(GetNumericFormulaCacheStats()), "clear inside callback empties idle cache and resets stats");
			const auto nested = EvaluateNumericFormula(expression,
				[](const std::string& name, double& output) { output = name == "anchor" ? 100 : 200; return true; },
				[](const std::string&, const double*, std::size_t, double& output) { output = 5; return true; });
			Check(nested.Succeeded() && nested.value == 405, "same-expression recursion remains valid after active clear");
			value = 3; return true;
		});
	Check(outer.Succeeded() && outer.value == 28 && anchorCalls == 1 && tailCalls == 1 && functionCalls == 1,
		"eviction and clear cannot destroy or overwrite active compiled code and bindings");
	Check(GetNumericFormulaCacheStats().entries <= NumericFormulaCacheCapacity, "returned leases respect idle capacity");
	const auto again = EvaluateNumericFormula(expression, symbols, functions);
	Check(again.Succeeded() && again.value == 28, "expression remains reusable after active clear");
}

/** Admit every entry, refresh the oldest, then prove actual least-recently-used eviction. */
void CachedIdleEviction()
{
	ClearNumericFormulaCache();
	double epoch = 10;
	const NumericSymbolResolver resolver = [&](const std::string& name, double& value) {
		value = epoch; return name == "epoch";
	};
	bool admitted = true;
	for (std::size_t i = 0; i < NumericFormulaCacheCapacity; ++i)
	{
		const std::string expression = "epoch + " + std::to_string(i);
		const auto before = GetNumericFormulaCacheStats();
		const auto first = EvaluateNumericFormula(expression, resolver);
		const auto second = EvaluateNumericFormula(expression, resolver);
		const auto after = GetNumericFormulaCacheStats();
		admitted = admitted && first.Succeeded() && second.Succeeded()
			&& first.value == float(epoch + i) && second.value == first.value
			&& after.misses == before.misses + 1 && after.hits == before.hits + 1
			&& after.entries == i + 1;
	}
	Check(admitted, "every idle entry was admitted and reusable before capacity pressure");
	ExpectValue("epoch + 0", 10, resolver);
	ExpectValue("epoch + " + std::to_string(NumericFormulaCacheCapacity),
		float(epoch + NumericFormulaCacheCapacity), resolver);
	Check(GetNumericFormulaCacheStats().entries == NumericFormulaCacheCapacity,
		"overflow insertion keeps exactly the bounded idle working set");
	epoch = 30;
	const auto beforeHot = GetNumericFormulaCacheStats();
	ExpectValue("epoch + 0", 30, resolver);
	Check(GetNumericFormulaCacheStats().hits == beforeHot.hits + 1,
		"recently refreshed oldest source survives capacity pressure");
	const auto beforeCold = GetNumericFormulaCacheStats();
	ExpectValue("epoch + 1", 31, resolver);
	Check(GetNumericFormulaCacheStats().misses == beforeCold.misses + 1,
		"least-recently-used idle source is evicted and recompiled with fresh bindings");
}

/** Runtime failures must match first use and must not poison a later successful cache hit. */
void CachedFailureRecovery()
{
	const std::array<Error, 6> errors{{Error::MissingResolver, Error::UnknownSymbol, Error::ResolverFailure,
		Error::NonFiniteValue, Error::NonFiniteValue, Error::NonFiniteValue}};
	for (std::size_t mode = 0; mode < errors.size(); ++mode)
	{
		NumericSymbolResolver badSymbol;
		NumericFunctionResolver badFunction;
		const auto fail = [mode](double& value) -> bool {
			if (mode == 1) return false;
			if (mode == 2) throw std::runtime_error("cache callback failure");
			if (mode == 4) value = std::numeric_limits<double>::infinity();
			if (mode == 5) value = std::numeric_limits<double>::quiet_NaN();
			return true;
		};
		if (mode)
		{
			badSymbol = [fail](const std::string&, double& value) { return fail(value); };
			badFunction = [fail](const std::string&, const double*, std::size_t, double& value) { return fail(value); };
		}
		ClearNumericFormulaCache();
		CheckCacheError(EvaluateNumericFormula("live + live", badSymbol), errors[mode], "cold symbol failure classification");
		ExpectValue("live + live", 8, [](const std::string&, double& value) { value = 4; return true; });
		CheckCacheError(EvaluateNumericFormula("live + live", badSymbol), errors[mode], "cached symbol failure matches first use");
		ExpectValue("live + live", 18, [](const std::string&, double& value) { value = 9; return true; });

		ClearNumericFormulaCache();
		CheckCacheError(EvaluateNumericFormula("random(1,2)", {}, badFunction), errors[mode], "cold function failure classification");
		const NumericFunctionResolver good = [](const std::string&, const double*, std::size_t, double& value) {
			value = 6; return true;
		};
		const auto warm = EvaluateNumericFormula("random(1,2)", {}, good);
		Check(warm.Succeeded() && warm.value == 6, "function success recovers from first-use failure");
		CheckCacheError(EvaluateNumericFormula("random(1,2)", {}, badFunction), errors[mode], "cached function failure matches first use");
		const auto recovered = EvaluateNumericFormula("random(1,2)", {}, good);
		Check(recovered.Succeeded() && recovered.value == 6, "function cache recovers after callback failure");
	}
	for (const auto& sample : {std::pair<const char*, double>{"1/live", 0}, {"sqrt(live)", -1}, {"live", 1e39}})
	{
		ClearNumericFormulaCache();
		const NumericSymbolResolver bad = [&](const std::string&, double& value) { value = sample.second; return true; };
		CheckCacheError(EvaluateNumericFormula(sample.first, bad), Error::NonFiniteValue, "cold arithmetic or narrowing failure");
		ExpectValue(sample.first, 1, [](const std::string&, double& value) { value = 1; return true; });
		CheckCacheError(EvaluateNumericFormula(sample.first, bad), Error::NonFiniteValue, "cached arithmetic or narrowing failure");
		ExpectValue(sample.first, 1, [](const std::string&, double& value) { value = 1; return true; });
	}
	for (const char* invalid : {"live +", "true ? 7 : random(1)", "unknown(1)"})
	{
		ClearNumericFormulaCache();
		const auto cold = EvaluateNumericFormula(invalid);
		Check(!cold.Succeeded() && cold.error != Error::InternalError && std::isnan(cold.value), "cold invalid syntax is checked");
		CheckCacheError(EvaluateNumericFormula(invalid), cold.error, "repeated invalid syntax preserves error classification");
		ExpectValue("2+3", 5);
	}
}

/** Idle entries cannot prolong engine-like captured owners on either success or exception paths. */
void CachedResolverLifetime()
{
	ClearNumericFormulaCache();
	for (bool throwing : {false, true})
	{
		std::weak_ptr<int> symbolOwner, functionOwner;
		{
			auto symbol = std::make_shared<int>(7), function = std::make_shared<int>(11);
			symbolOwner = symbol; functionOwner = function;
			const auto result = EvaluateNumericFormula("held + random(1,2)",
				[symbol](const std::string& name, double& value) { value = *symbol; return name == "held"; },
				[function, throwing](const std::string&, const double*, std::size_t, double& value) {
					if (throwing) throw std::runtime_error("captured context failure");
					value = *function; return true;
				});
			if (throwing) CheckCacheError(result, Error::ResolverFailure, "captured throwing resolver remains contained");
			else Check(result.Succeeded() && result.value == 18, "captured resolvers evaluate normally");
		}
		Check(symbolOwner.expired() && functionOwner.expired(), "idle cache retains neither symbol nor function callable ownership");
		CheckCacheError(EvaluateNumericFormula("held + random(1,2)"), Error::MissingResolver,
			"cache cannot reuse destroyed resolver captures");
	}
}

/** Worker caches start empty and one thread's clear cannot change another thread's idle code or stats. */
void CachedThreadIsolation()
{
	ClearNumericFormulaCache();
	ExpectValue("mainValue + 1", 5, [](const std::string&, double& value) { value = 4; return true; });
	ExpectValue("mainValue + 1", 9, [](const std::string&, double& value) { value = 8; return true; });
	const auto mainStats = GetNumericFormulaCacheStats();
	std::array<bool, 2> passed{{true, true}};
	std::array<std::promise<void>, 2> ready;
	std::promise<void> release;
	const auto released = release.get_future().share();
	std::vector<std::thread> workers;
	for (std::size_t i = 0; i < passed.size(); ++i)
		workers.emplace_back([&, i] {
			passed[i] = EmptyCacheStats(GetNumericFormulaCacheStats());
			for (int iteration = 0; iteration < 32; ++iteration)
			{
				const double live = i * 100 + iteration;
				const auto before = GetNumericFormulaCacheStats();
				const auto result = EvaluateNumericFormula("threadValue + random(1,2)",
					[live](const std::string& name, double& value) { value = live; return name == "threadValue"; },
					[live](const std::string&, const double*, std::size_t, double& value) { value = live + 3; return true; });
				const auto after = GetNumericFormulaCacheStats();
				passed[i] = passed[i] && result.Succeeded() && result.value == float(2 * live + 3)
					&& after.entries <= NumericFormulaCacheCapacity && (!iteration || after.hits > before.hits);
			}
			const auto beforeClearElsewhere = GetNumericFormulaCacheStats();
			ready[i].set_value();
			released.wait();
			passed[i] = passed[i] && SameCacheStats(beforeClearElsewhere, GetNumericFormulaCacheStats());
			ClearNumericFormulaCache();
			passed[i] = passed[i] && EmptyCacheStats(GetNumericFormulaCacheStats());
		});
	for (auto& signal : ready) signal.get_future().wait();
	Check(SameCacheStats(mainStats, GetNumericFormulaCacheStats()), "worker evaluation cannot change caller-thread cache stats");
	ClearNumericFormulaCache();
	Check(EmptyCacheStats(GetNumericFormulaCacheStats()), "caller clear while workers hold idle entries resets only caller cache");
	release.set_value();
	for (auto& worker : workers) worker.join();
	for (bool result : passed) Check(result, "thread-local reuse, callback freshness and cross-thread clear isolation");
}

/** Exercise the public cache contract before the existing numeric/lowering corpus. */
void CompiledFormulaCache()
{
	CachedFreshBindings();
	CachedBranchChoices();
	CachedRecursiveBindings();
	CachedActiveEvictionAndClear();
	CachedIdleEviction();
	CachedFailureRecovery();
	CachedResolverLifetime();
	CachedThreadIsolation();
	ClearNumericFormulaCache();
}
