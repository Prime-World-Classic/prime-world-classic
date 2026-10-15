#include "formula_numeric.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace LinuxBootstrap;
using Error = NumericFormulaError;
std::size_t checks = 0;

/** Assertions remain active under NDEBUG and print the offending expression. */
void Check(bool condition, const std::string& label)
{
	++checks;
	if (condition) return;
	std::fprintf(stderr, "Numeric formula failed: %s (check %zu)\n", label.c_str(), checks);
	std::exit(1);
}

/** Float-reference comparisons allow only rounding-sized error, not integer truncation. */
bool Close(float actual, float expected)
{
	const float scale = std::fmax(1.0f, std::fabs(expected));
	return std::isfinite(actual) && std::fabs(actual - expected) <=
		2 * std::numeric_limits<float>::epsilon() * scale;
}

void ExpectValue(const std::string& expression, float expected,
	const NumericSymbolResolver& resolver = {})
{
	const auto result = EvaluateNumericFormula(expression, resolver);
	if (!result.Succeeded() || !Close(result.value, expected))
		std::fprintf(stderr, "Expected %.9g, got %.9g, status %d\n", expected, result.value, int(result.error));
	Check(result.Succeeded() && Close(result.value, expected), expression);
}

void ExpectError(const std::string& expression, Error error,
	const NumericSymbolResolver& resolver = {})
{
	const auto result = EvaluateNumericFormula(expression, resolver);
	Check(!result.Succeeded() && result.error == error && std::isnan(result.value), expression);
}

void ExpectFailure(const std::string& expression)
{
	const auto result = EvaluateNumericFormula(expression);
	Check(!result.Succeeded() && std::isnan(result.value), expression);
}

/** Literal suffixes are lexical, not a postfix operator available on arbitrary values. */
void LiteralsAndOperators()
{
	ExpectValue("0", 0.0f);
	ExpectValue("-0.0f", -0.0f);
	Check(std::signbit(EvaluateNumericFormula("-0.0f").value), "negative zero preserved");
	ExpectValue(" \t70.0f\r\n", 70.0f);
	ExpectValue(".5F", 0.5f);
	ExpectValue("1.f", 1.0f);
	ExpectValue("1e-2f", 0.01f);
	ExpectValue("1.25E+2", 125.0f);
	ExpectValue("1f", 1.0f);
	ExpectValue("16777217.0f - 16777216.0f", 0.0f);
	ExpectValue("16777217.0 - 16777216.0", 1.0f);
	ExpectValue("2 + 3 * 4", 2.0f + 3.0f * 4.0f);
	ExpectValue("(2 + 3) * 4", (2.0f + 3.0f) * 4.0f);
	ExpectValue("20 / 5 / 2", 20.0f / 5.0f / 2.0f);
	ExpectValue("10 - 3 - 2", 10.0f - 3.0f - 2.0f);
	ExpectValue("1/2", 0.5f);
	ExpectValue("-(-2)", 2.0f);
	ExpectValue("1- -2", 3.0f);
	ExpectValue("+2 * -3 + 8", 2.0f * -3.0f + 8.0f);
	ExpectValue("1 == 2 < 3", 1.0f == (2.0f < 3.0f));
	ExpectValue("2 > 1 == 3 > 2", (2.0f > 1.0f) == (3.0f > 2.0f));
	ExpectValue("1 != 2 == 1", (1.0f != 2.0f) == 1.0f);
	ExpectValue("2 <= 2", 1.0f);
	ExpectValue("2 >= 3", 0.0f);
	ExpectValue("0 ? 1 : 0 ? 2 : 3", 3.0f);
	ExpectValue("1 ? 0 ? 2 : 3 : 4", 3.0f);
	ExpectValue("-1 ? 2 : 3", 2.0f);
	ExpectValue("0 ? 2 : 3 + 4", 7.0f);
	ExpectValue("1e300 / 1e300", 1.0f);
}

/** Logical lowering retains C++ precedence, nonzero truth and lazy side-effect boundaries. */
void LogicalOperators()
{
	for (double a : {0., -.0, .5, -2.}) for (double b : {0., .5, -2.}) for (double c : {0., .5, -2.})
	{
		const NumericSymbolResolver resolver = [&](const std::string& name, double& value) {
			value = name == "a" ? a : name == "b" ? b : c; return true;
		};
		ExpectValue("a && b", a && b, resolver);
		ExpectValue("a || b", a || b, resolver);
		ExpectValue("!a", !a, resolver);
		ExpectValue("!!a", !!a, resolver);
		ExpectValue("a || b && c", a || (b && c), resolver);
		ExpectValue("a && b || c", (a && b) || c, resolver);
		ExpectValue("a == b || c", (a == b) || c, resolver);
		ExpectValue("!a == b", (!a) == b, resolver);
		ExpectValue("a || b ? 3 : 4", (a || b) ? 3 : 4, resolver);
		ExpectValue("a ? b || c : !c", a ? (b || c) : !c, resolver);
		ExpectValue("min(a || b, !c)", std::fmin(double(a || b), double(!c)), resolver);
		ExpectValue("(a + b) && (b - c)", (a + b) && (b - c), resolver);
	}
	std::vector<std::string> trace;
	const NumericSymbolResolver resolver = [&](const std::string& name, double& value) {
		trace.push_back(name); value = name == "on" ? .5 : 0; return name != "missing";
	};
	ExpectValue("off && missing", 0, resolver);
	Check(trace == std::vector<std::string>{"off"}, "false AND skips its RHS callback");
	trace.clear(); ExpectValue("on || missing", 1, resolver);
	Check(trace == std::vector<std::string>{"on"}, "nonzero fractional OR skips its RHS callback");
	trace.clear(); ExpectValue("on && off || on", 1, resolver);
	Check(trace == std::vector<std::string>({"on", "off"}), "logical operands retain per-symbol caching and first-resolution order");
	ExpectError("on && missing", Error::UnknownSymbol, resolver);
	ExpectError("off || missing", Error::UnknownSymbol, resolver);
	ExpectValue("false && (1/0)", 0); ExpectValue("true || sqrt(-1)", 1);
	ExpectValue("!0 ? 1 : missing", 1); ExpectValue("!(1 || missing)", 0);
	int randomCalls = 0;
	const NumericFunctionResolver random = [&](const std::string&, const double*, std::size_t, double& value) {
		++randomCalls; value = 2; return true;
	};
	Check(EvaluateNumericFormula("false && random(1,2)", {}, random).value == 0 && randomCalls == 0,
		"short-circuited logical RNG is never invoked");
	Check(EvaluateNumericFormula("true && random(1,2)", {}, random).value == 1 && randomCalls == 1,
		"selected logical RNG is invoked once and normalized to boolean");
	ExpectFailure("true || random(1)"); ExpectFailure("false && unknown(1)");
	ExpectFailure("true || (1 + )"); ExpectFailure("false && 1e400");
	for (const char* bad : {"!", "1!2", "1 &&", "||1", "1&&&2", "1|||2", "1 & 2", "1 | 2"}) ExpectFailure(bad);
	ExpectValue(std::string(NumericFormulaMaxNesting, '!') + "0", 0);
	ExpectError(std::string(NumericFormulaMaxNesting + 1, '!') + "0", Error::LimitExceeded);
	std::string chain = "true";
	for (std::size_t i = 0; i < NumericFormulaMaxNesting; ++i) chain += " && true";
	ExpectValue(chain, 1); ExpectError(chain + " || false", Error::LimitExceeded);
	ExpectFailure("true && - -2");
}

/** Deterministic mixed-expression corpus checks lowering against independent scalar expectations. */
void LogicalCorpus()
{
	struct Node { std::string source; double value; };
	std::uint32_t seed = 0x4c4f4749;
	const auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
	const auto make = [&](const auto& self, int depth) -> Node {
		if (depth == 0)
		{
			const double value = (static_cast<int>(next() % 9) - 4) * .5;
			return {std::to_string(value), value};
		}
		const int operation = next() % 10;
		const Node a = self(self, depth - 1), b = self(self, depth - 1);
		const std::string left = "(" + a.source + ")", right = "(" + b.source + ")";
		switch (operation)
		{
		case 0: return {left + "&&" + right, double(a.value && b.value)};
		case 1: return {left + "||" + right, double(a.value || b.value)};
		case 2: return {"!" + left, double(!a.value)};
		case 3: return {left + "+" + right, a.value + b.value};
		case 4: return {left + "-" + right, a.value - b.value};
		case 5: return {left + "*" + right, a.value * b.value};
		case 6: return {left + "<" + right, double(a.value < b.value)};
		case 7: return {left + "==" + right, double(a.value == b.value)};
		case 8: return {"max(" + left + "," + right + ")", std::fmax(a.value, b.value)};
		default: return {left + "?" + right + ":0.5", a.value ? b.value : .5};
		}
	};
	for (int i = 0; i < 1000; ++i)
	{
		const Node node = make(make, 3);
		ExpectValue("!!(" + node.source + ")", node.value != 0);
	}
}

/** Exact native Data formula; no dependency on the parent's engine or CMake work. */
void PlaneRange()
{
	const std::string expression =
		"(sBaseStrength>=sBaseIntellect)? sRange : sRange+5+(sIntellect/80)";
	struct Stats { float strength; float baseIntellect; float intellect; float range; };
	Stats stats{55, 54, 54, 14};
	std::map<std::string, int> calls;
	const NumericSymbolResolver resolver = [&](const std::string& name, double& value)
	{
		++calls[name];
		if (name == "sBaseStrength") value = stats.strength;
		else if (name == "sBaseIntellect") value = stats.baseIntellect;
		else if (name == "sIntellect") value = stats.intellect;
		else if (name == "sRange") value = stats.range;
		else return false;
		return true;
	};
	ExpectValue(expression, 14, resolver);
	Check(calls["sIntellect"] == 0, "Plane strength branch does not read current intellect");
	const std::array<Stats, 6> fixtures{{{55, 54, 54, 14}, {55, 55, 160, 14},
		{54, 55, 80, 14}, {54, 55, 40, 14}, {55, 54, 200, 14}, {54, 55, 160, 18}}};
	for (const auto& fixture : fixtures)
	{
		stats = fixture;
		const float expected = stats.strength >= stats.baseIntellect ? stats.range :
			stats.range + 5 + stats.intellect / 80;
		ExpectValue(expression, expected, resolver);
	}
	for (float base : {0.0f, 54.0f, 55.0f, 160.0f})
	for (float strength : {0.0f, 54.0f, 55.0f, 160.0f})
	for (float intellect : {0.0f, 0.1f, 40.0f, 79.9f, 80.0f, 160.0f, 799.5f})
	for (float range : {0.0f, 0.01f, 14.0f, 18.0f, 25.25f})
	{
		stats = {strength, base, intellect, range};
		const float expected = strength >= base ? range : range + 5 + intellect / 80;
		ExpectValue(expression, expected, resolver);
	}
}

/** Validation covers all branches, but live values are requested only on the taken path. */
void LazySymbols()
{
	int calls = 0;
	double live = 4;
	const NumericSymbolResolver resolver = [&](const std::string& name, double& value)
	{
		++calls;
		if (name != "live") return false;
		value = live;
		return true;
	};
	ExpectValue("1 ? live : missing", 4, resolver);
	Check(calls == 1, "unused unknown symbol is not resolved");
	ExpectValue("0 ? missing : live + live", 8, resolver);
	Check(calls == 2, "one symbol snapshot per evaluation");
	live = 9;
	ExpectValue("live + live", 18, resolver);
	Check(calls == 3, "repeated calls refresh state");
	ExpectError("missing", Error::UnknownSymbol, resolver);
	ExpectError("live", Error::MissingResolver);
	ExpectValue("0 ? missing : 14", 14);
	ExpectValue("1 ? 14 : (1 / 0)", 14);
	ExpectValue("0 ? (1e300 * 1e300) : 14", 14);
	ExpectValue("0 ? 1 / 0 : 1 ? live : missing", 9, resolver);
	const int before = calls;
	ExpectError("live +", Error::InvalidExpression, resolver);
	ExpectError("1 ? live : missing(0)", Error::UnsupportedSyntax, resolver);
	Check(calls == before, "bad syntax has no resolver side effects");
	ExpectValue("plain_9", 7, [](const std::string& name, double& value)
	{
		value = 7;
		return name == "plain_9";
	});
	ExpectError("_pi", Error::UnknownSymbol, resolver);
	ExpectValue("pw_symbol_0 + pw_number_0", 4,
		[](const std::string&, double& value) { value = 2; return true; });
}

/** Explicit helpers use authored argument order and reject hidden invalid intermediates. */
void NumericFunctions()
{
	ExpectValue("min(4, 9)", 4);
	ExpectValue("f2l(-3.9)", -3);
	ExpectValue("f2l(3.9)", 3);
	ExpectValue("f2l(-2147483648)", -2147483648.f);
	ExpectError("f2l(2147483648)", Error::NonFiniteValue);
	ExpectError("f2l(-2147483904)", Error::NonFiniteValue);
	ExpectValue("1 ? 2 : f2l(1e39)", 2);
	ExpectValue("min \t (4, 9)", 4);
	ExpectValue("max(4, 9)", 9);
	ExpectValue("clamp(-1, 2, 8)", 2);
	ExpectValue("clamp(12, 2, 8)", 8);
	ExpectValue("clamp(5, 8, 2)", 2);
	ExpectValue("lerp(.25, 10, 30)", 15);
	ExpectValue("lerp(2, 10, 30)", 50);
	ExpectValue("min(max(1, 4), clamp(8, 0, 6))", 4);
	ExpectValue("abs(-3.5) + sqrt(2.25)", 5);
	ExpectValue("floor(-1.2) + ceil(-1.2)", -3);
	ExpectValue("round(2.5) + round(-1.5)", 1);
	ExpectValue("true ? 3 : false", 3);
	ExpectValue("false ? 1/0 : 7", 7);
	ExpectValue("1 ? 5 : sqrt(-1)", 5);
	ExpectValue("0 ? min(1e39, 2) : 6", 6);
	ExpectValue("max((1 ? 2 : 3), 4)", 4);
	ExpectValue("max(1, 0 ? 2 : 3)", 3);
	ExpectValue("min(16777217, 16777218)-16777216", 0);
	int calls = 0;
	const NumericSymbolResolver resolver = [&](const std::string& name, double& value) {
		++calls; value = 4; return name == "live";
	};
	ExpectValue("max(live,live)+min(live,8)", 8, resolver);
	Check(calls == 1, "function arguments retain per-evaluation symbol snapshot");
	ExpectValue("0 ? max(missing,live) : 8", 8, resolver);
	Check(calls == 1, "unused function arguments are lazy");
	for (const char* expression : {"min()", "min(1)", "min(1,2,3)", "max(,1)",
		"max(1,)", "max((1,2),3)", "(max(1,2),3)", "max(1,2),3",
		"1 ? 2 : min(1)", "min(1,2)(3)", "floor(1,2)", "clamp(1,2)",
		"lerp(1,2,3,4)", "true(1)", "min(1;2)", "min(1,max(2,3)),4"})
		ExpectFailure(expression);
	for (const char* expression : {"sqrt(-1)", "min(1/0,2)", "max(1,0/0)",
		"clamp(1e39,0,1)", "lerp(1e38,1e38,1e38)", "abs(1e300*1e300)"})
		ExpectError(expression, Error::NonFiniteValue);
	for (int a = -8; a <= 8; ++a)
	for (int b = -8; b <= 8; ++b)
	{
		const std::string args = "(" + std::to_string(a) + "," + std::to_string(b) + ")";
		ExpectValue("min" + args, float(a < b ? a : b));
		ExpectValue("max" + args, float(a > b ? a : b));
	}
	for (float value : {-8.5f, -2.5f, -.5f, -.49f, 0.f, .49f, .5f, 2.5f, 8.5f, 8388609.f})
		ExpectValue("round(" + std::to_string(value) + ")",
			value >= 0 ? std::floor(value + .5f) : std::ceil(value - .5f));
	std::string nested = "1";
	for (std::size_t i = 0; i < NumericFormulaMaxNesting; ++i) nested = "abs(" + nested + ")";
	ExpectValue(nested, 1);
	ExpectError("abs(" + nested + ")", Error::LimitExceeded);
}

/** Context function transport is bounded, lazy, exception-contained and evaluation-local. */
void ContextFunctions()
{
	// Original nt is a lazy macro, including when branches contain context calls.
	for (const bool native : {false, true})
	{
		int reads = 0, calls = 0;
		const NumericSymbolResolver terrain = [&](const std::string& name, double& value) {
			++reads;
			value = name == "sTerrainType" ? (native ? 1 : 2) : 1;
			return name == "sTerrainType" || name == "sFctn";
		};
		const NumericFunctionResolver history = [&](const std::string& name, const double* args, std::size_t count, double& value) {
			++calls; value = args[0]; return name == "__history_s_taken" && count == 1;
		};
		const std::string live = "__history_s_taken(9)", bad = "__history_s_taken(1/0)";
		const std::string expression = native ? "nt(" + live + "," + bad + ")" : "nt(" + bad + "," + live + ")";
		Check(EvaluateNumericFormula(expression, terrain, history).value == 9 && reads == 2 && calls == 1,
			"terrain macro evaluates only the selected history branch");
		reads = 0; calls = 0;
		Check(EvaluateNumericFormula(expression + "+nt(2,3)+sTerrainType", terrain, history).value == (native ? 12 : 14)
			&& reads == 2 && calls == 1, "injected terrain getters share the source symbol snapshot");
		for (const char* suffix : {"()", "(1)", "(1,2,3)", "(,1)", "(1,)"})
			Check(!EvaluateNumericFormula("0 ? nt" + std::string(suffix) + " : 2", terrain, history).Succeeded()
				&& reads == 2 && calls == 1, "terrain arity is validated before resolving getters");
	}
	for (const char* name : {"__history_s_taken_ex", "__history_s_dealt_ex", "__history_r_taken_ex", "__history_r_dealt_ex",
		"__history_s_taken_ex_misc", "__history_s_dealt_ex_misc", "__history_r_taken_ex_misc", "__history_r_dealt_ex_misc"})
	{
		int calls = 0;
		const std::string prefix(name);
		const NumericFunctionResolver extended = [&](const std::string& call, const double* args, std::size_t count, double& value) {
			++calls; if (call != name || count != 2) return false;
			value = args[0] + args[1]; return true;
		};
		Check(EvaluateNumericFormula(prefix + "(min(2,3),4)", {}, extended).value == 6 && calls == 1,
			"extended history transport preserves its two numeric arguments");
		for (const char* suffix : {"()", "(1)", "(1,2,3)", "(1,,2)", "(,2)"})
			Check(!EvaluateNumericFormula("0 ? " + prefix + suffix + " : 9", {}, extended).Succeeded() && calls == 1,
				"extended history arity and missing arguments are validated in skipped branches");
		Check(EvaluateNumericFormula("1 || " + prefix + "(1/0,missing)", {}, extended).value == 1 && calls == 1,
			"extended history remains lazy under logical lowering");
		Check(!EvaluateNumericFormula(prefix + "(1/0,4)", {}, extended).Succeeded() && calls == 1,
			"invalid selected extended argument fails before callback");
		Check(!EvaluateNumericFormula(prefix + "(1,2)").Succeeded(), "selected extended transport requires a resolver");
	}
	for (const char* name : {"__history_s_taken", "__history_s_dealt", "__history_r_taken", "__history_r_dealt"})
	{
		int calls = 0;
		const NumericFunctionResolver history = [&](const std::string& call, const double* args, std::size_t count, double& value) {
			++calls; value = args[0]; return call == name && count == 1;
		};
		const std::string prefix = name;
		Check(EvaluateNumericFormula(prefix + "(min(3,7))", {}, history).value == 3 && calls == 1,
			"reserved history transport passes one evaluated scalar argument");
		for (const char* suffix : {"()", "(1,2)", "(,1)", "(1,)", "(1,,2)"})
			Check(!EvaluateNumericFormula("0 ? " + prefix + suffix + " : 2", {}, history).Succeeded() && calls == 1,
				"one-argument history arity is checked even in unselected branches");
		Check(EvaluateNumericFormula("0 ? " + prefix + "(1/0) : 4").value == 4,
			"unselected history argument requires no resolver");
		Check(EvaluateNumericFormula(prefix + "(1)").error == Error::MissingResolver,
			"selected scalar history call needs explicit context");
		Check(EvaluateNumericFormula(prefix + "(1/0)", {}, history).error == Error::NonFiniteValue && calls == 1,
			"invalid history argument fails before the callback");
		Check(EvaluateNumericFormula(prefix + "(1)+" + prefix + "(2)", {}, history).value == 3 && calls == 3,
			"separate selected history calls are not cached");
	}
	for (const char* name : {"ut_bldg", "ut_hero"})
	{
		int predicateCalls = 0;
		const NumericFunctionResolver predicate = [&](const std::string& call, const double* args, std::size_t count, double& value) {
			++predicateCalls; value = args[0]; return call == name && count == 2;
		};
		const std::string prefix = name;
		Check(EvaluateNumericFormula(prefix + "(3,7)", {}, predicate).value == 3 && predicateCalls == 1,
			"receiver predicate transports two scalar arguments");
		for (const char* suffix : {"()", "(1)", "(1,2,3)", "(,1)", "(1,)"})
			Check(!EvaluateNumericFormula("0 ? " + prefix + suffix + " : 2", {}, predicate).Succeeded() && predicateCalls == 1,
				"receiver predicate arity remains checked in unused branches");
		Check(EvaluateNumericFormula("0 ? " + prefix + "(1/0,missing) : 4").value == 4,
			"unselected receiver predicate needs no context");
	}
	int randomCalls = 0;
	const NumericFunctionResolver random = [&](const std::string& name, const double* args, std::size_t count, double& value) {
		++randomCalls; value = args[1]; return name == "random" && count == 2;
	};
	Check(EvaluateNumericFormula("random(3,7)", {}, random).value == 7 && randomCalls == 1, "two-argument random callback");
	for (const char* expression : {"random()", "random(1)", "random(1,2,3)", "0 ? random(1) : 2"})
		Check(!EvaluateNumericFormula(expression, {}, random).Succeeded() && randomCalls == 1, "random arity checked before callback");
	Check(EvaluateNumericFormula("1 ? 4 : random(1,2)").value == 4, "unselected random requires no callback");
	int calls = 0;
	const NumericFunctionResolver resolver = [&](const std::string& name, const double* args,
		std::size_t count, double& value) {
		++calls;
		Check((name == "abilityScale" || name == "damageScale") && (count == 3 || count == 4), "only whitelisted callback shapes");
		value = args[0] + 10 * args[1] + 100 * args[2] + (count == 4 ? 1000 * args[3] : 0);
		return true;
	};
	const auto expect = [&](const char* expression, float value) {
		const auto result = EvaluateNumericFormula(expression, {}, resolver);
		Check(result.Succeeded() && result.value == value, expression);
	};
	expect("abilityScale(1,2,3)", 321);
	expect("damageScale(1,2,3,true)", 1321);
	expect("abilityScale(damageScale(1,2,3),2,3,false)", 641);
	const int before = calls;
	expect("0 ? abilityScale(1,2,3) : 7", 7);
	Check(calls == before, "unselected context functions are lazy");
	for (const char* expression : {"abilityScale(1,2)", "1 ? 2 : damageScale(1,2)",
		"abilityScale(1,2,3,4,5)", "abilityScale(1,,3)", "abilityScale(1,2,3),4",
		"damageScale((1,2),3,4)", "abilityScale(1,2,3)+", "unknown(1,2,3)"})
	{
		Check(!EvaluateNumericFormula(expression, {}, resolver).Succeeded(), expression);
		Check(calls == before, "invalid call syntax invokes no resolver");
	}
	ExpectError("abilityScale(1,2,3)", Error::MissingResolver);
	ExpectValue("1 ? 4 : abilityScale(1,2,3)", 4);
	Check(EvaluateNumericFormula("abilityScale(1,2,3)", {},
		[](const std::string&, const double*, std::size_t, double&) { return false; }).error == Error::UnknownSymbol, "missing function context");
	Check(EvaluateNumericFormula("abilityScale(1,2,3)", {},
		[](const std::string&, const double*, std::size_t, double&) { return true; }).error == Error::NonFiniteValue, "unwritten callback output");
	const NumericFunctionResolver throwing = [](const std::string&, const double*, std::size_t, double&) -> bool {
		throw std::runtime_error("unavailable scaling context");
	};
	Check(EvaluateNumericFormula("abilityScale(1,2,3)", {}, throwing).error == Error::ResolverFailure, "callback exceptions contained");
	Check(EvaluateNumericFormula("0 ? abilityScale(1,2,3) : 4", {}, throwing).value == 4, "unselected throwing callback");
	Check(EvaluateNumericFormula("abilityScale(1/0,2,3)", {}, resolver).error == Error::NonFiniteValue, "invalid argument stops callback");
	Check(calls == before, "failed argument does not reach context");
	expect("abilityScale(1,2,3)+abilityScale(1,2,3)", 642);
	Check(calls == before + 2, "each selected call executes separately");
	const auto recursive = EvaluateNumericFormula("abilityScale(1,2,3)", {},
		[](const std::string&, const double*, std::size_t, double& value) {
			const auto nested = EvaluateNumericFormula("max(4,7)"); value = nested.value; return nested.Succeeded();
		});
	Check(recursive.Succeeded() && recursive.value == 7, "recursive callback owns independent parser state");
}

/** Unsupported C++/muParser surfaces must fail instead of acquiring new semantics. */
void RejectedSyntax()
{
	for (const char* expression : {"x=1", "x+=1", "1,2", "(1,2)", "1?2:3,4", "\"x\"",
		"'x'", "rnd()", "sin(0)", "random(0,1)", "tal(\"Plane_A1u\")", "live ()",
		"1^2", "1**2", "1&2", "1|2", "1<<2", "1>>2", "~1",
		"5%2", "x++", "--x", "1//2", "1/*x*/+2", "1;2", "x[0]", "x.y",
		"p->x", "0x10", "0b10", "010", "1u", "1L", "1.0ff", "1.0 f", "1e+",
		"(float)1", "nullptr", "1?2:bad()", "1?2:1^2"})
		ExpectFailure(expression);
	for (const char* expression : {"(", ")", "()", "1+", "*1", "1 2", "1(2)",
		"live live", "1?2", "1:2", "1?2:", "?1:2", "1??2:3", "((1)", "1)", "- -2"})
		ExpectFailure(expression);
	ExpectError("", Error::EmptyExpression);
	ExpectError(" \t\r\n", Error::EmptyExpression);
	ExpectFailure(std::string("1\0+2", 4));
	ExpectFailure(std::string("1+") + char(0xff));
	ExpectFailure("1\v+2");
	ExpectFailure("1\f+2");
}

/** Invalid inputs cannot become a finite comparison result or conditional predicate. */
void NumericFailures()
{
	ExpectError("1/0", Error::NonFiniteValue);
	ExpectError("0/0", Error::NonFiniteValue);
	ExpectError("(1/0) > 0", Error::NonFiniteValue);
	ExpectError("(0/0) ? 1 : 2", Error::NonFiniteValue);
	ExpectError("1e300 * 1e300", Error::NonFiniteValue);
	ExpectError("1e39", Error::NonFiniteValue);
	ExpectError("1e39f", Error::NonFiniteValue);
	ExpectError("1e309", Error::InvalidNumber);
	ExpectError("1e-999", Error::InvalidNumber);
	for (double value : {std::numeric_limits<double>::infinity(),
		-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		const NumericSymbolResolver resolver = [=](const std::string&, double& output)
		{
			output = value;
			return true;
		};
		ExpectError("bad == bad", Error::NonFiniteValue, resolver);
		ExpectValue("1 ? 2 : bad", 2, resolver);
	}
	ExpectError("unwritten", Error::NonFiniteValue,
		[](const std::string&, double&) { return true; });
	ExpectError("missing", Error::UnknownSymbol,
		[](const std::string&, double& value) { value = 5; return false; });
	const NumericSymbolResolver throwing = [](const std::string&, double&) -> bool
	{
		throw std::runtime_error("unavailable context");
	};
	ExpectError("x", Error::ResolverFailure, throwing);
	ExpectValue("0 ? x : 1", 1, throwing);
	ExpectValue("2", 2);
}

/** Limits are lexical budgets, including branches which will not execute. */
void Bounds()
{
	Check(!NumericFormulaResult{}.Succeeded() && std::isnan(NumericFormulaResult{}.value),
		"default result cannot be mistaken for success");
	ExpectValue(std::string(NumericFormulaMaxLength - 1, ' ') + "1", 1);
	ExpectError(std::string(NumericFormulaMaxLength, ' ') + "1", Error::LimitExceeded);
	ExpectValue(std::string(NumericFormulaMaxNesting, '(') + "1" +
		std::string(NumericFormulaMaxNesting, ')'), 1);
	ExpectError(std::string(NumericFormulaMaxNesting + 1, '(') + "1" +
		std::string(NumericFormulaMaxNesting + 1, ')'), Error::LimitExceeded);
	std::string conditional = "1";
	for (std::size_t i = 0; i < NumericFormulaMaxNesting; ++i) conditional = "0?0:" + conditional;
	ExpectValue(conditional, 1);
	ExpectError("0?0:" + conditional, Error::LimitExceeded);
	std::string signs = "1";
	for (std::size_t i = 0; i < NumericFormulaMaxNesting; ++i) signs = "- " + signs;
	// The lexical cap must reject before the parser's repeated-unary grammar check.
	ExpectError("- " + signs, Error::LimitExceeded);
	std::string tokens = "1";
	for (std::size_t i = 0; i < NumericFormulaMaxTokens / 2 - 1; ++i) tokens += "+1";
	ExpectValue(tokens, NumericFormulaMaxTokens / 2);
	ExpectError(tokens + "+1", Error::LimitExceeded);
	const NumericSymbolResolver resolver = [](const std::string&, double& value)
	{
		value = 1;
		return true;
	};
	ExpectValue(std::string(NumericFormulaMaxIdentifier, 'x'), 1, resolver);
	ExpectError(std::string(NumericFormulaMaxIdentifier + 1, 'x'), Error::LimitExceeded, resolver);
	std::string symbols = "x0";
	for (std::size_t i = 1; i < NumericFormulaMaxSymbols; ++i) symbols += "+x" + std::to_string(i);
	ExpectValue(symbols, NumericFormulaMaxSymbols, resolver);
	ExpectError(symbols + "+one_more", Error::LimitExceeded, resolver);
}

/** Reproducible malformed-token coverage; muParser, not this test, decides grammar. */
void TokenCorpus()
{
	const std::array<const char*, 22> tokens{{"0", "1", "2.5f", "x", "missing", "+", "-",
		"*", "/", "<", ">=", "==", "!=", "(", ")", "?", ":", "=", ",", "^", "rnd()", " "}};
	std::uint32_t state = 0x41a10007u;
	const auto next = [&]() { state = state * 1664525u + 1013904223u; return state; };
	const NumericSymbolResolver resolver = [](const std::string& name, double& value)
	{
		value = 3;
		return name == "x";
	};
	for (unsigned i = 0; i < 4000; ++i)
	{
		std::string expression;
		const unsigned length = 1 + next() % 24;
		for (unsigned token = 0; token < length; ++token)
		{
			expression += tokens[next() % tokens.size()];
			expression += ' ';
		}
		const auto first = EvaluateNumericFormula(expression, resolver);
		const auto second = EvaluateNumericFormula(expression, resolver);
		Check(first.error == second.error && (first.Succeeded() ?
			std::isfinite(first.value) && first.value == second.value :
			std::isnan(first.value) && std::isnan(second.value)), expression);
		Check(first.error != Error::InternalError, "token corpus has no unexpected exceptions: " + expression);
	}
}

/** Independent parser/binding ownership must survive recursive calls and concurrent callers. */
void Ownership()
{
	ExpectValue("nested + nested", 10, [](const std::string&, double& value)
	{
		const auto result = EvaluateNumericFormula("2+3");
		value = result.value;
		return result.Succeeded();
	});
	std::array<int, 4> failures{};
	std::vector<std::thread> workers;
	for (std::size_t i = 0; i < failures.size(); ++i)
		workers.emplace_back([&, i]
		{
			for (int j = 0; j < 200; ++j)
			{
				const double live = double(i) + j;
				const auto result = EvaluateNumericFormula("x > 10 ? x/2 : x+1",
					[=](const std::string&, double& value) { value = live; return true; });
				if (!result.Succeeded() || !Close(result.value,
					float(live > 10 ? live / 2 : live + 1))) ++failures[i];
			}
		});
	for (auto& worker : workers) worker.join();
	for (int failed : failures) Check(failed == 0, "concurrent state isolation");
}
}

/** The scalar parser transports refinement arguments without inventing a talent or caching context. */
static void RefinementFunctions()
{
	int calls = 0;
	const NumericFunctionResolver resolver = [&](const std::string& name, const double* args, std::size_t count, double& value) {
		++calls; value = args[0] + 10 * args[1]; return name == "refineScale" && count == 2;
	};
	const auto result = EvaluateNumericFormula("refineScale(3,min(2,4))", {}, resolver);
	Check(result.Succeeded() && result.value == 23 && calls == 1, "refinement passes two evaluated arguments to the real context callback");
	for (const char* suffix : {"()", "(1)", "(1,2,3)", "(,1)", "(1,)", "(1,,2)"})
		Check(!EvaluateNumericFormula(std::string("true ? 7 : refineScale") + suffix, {}, resolver).Succeeded() && calls == 1,
			"refinement arity remains invalid even when its branch is not selected");
	ExpectError("refineScale(1,2)", Error::MissingResolver);
	Check(EvaluateNumericFormula("true ? 7 : refineScale(1/0,missing)").value == 7, "unselected refinement requires neither arguments nor context");
	Check(EvaluateNumericFormula("refineScale(1/0,2)", {}, resolver).error == Error::NonFiniteValue && calls == 1,
		"nonfinite refinement argument fails before context invocation");
	Check(EvaluateNumericFormula("refineScale(1,2)+refineScale(1,2)", {}, resolver).value == 42 && calls == 3,
		"selected refinement calls are not folded or cached across evaluations");
	Check(EvaluateNumericFormula("refineScale(1,2)", {}, [](const std::string&, const double*, std::size_t, double& value) {
		value = std::numeric_limits<double>::infinity(); return true;
	}).error == Error::NonFiniteValue, "invalid refinement callback result cannot escape scalar validation");
}

int main()
{
	LiteralsAndOperators();
	LogicalOperators();
	LogicalCorpus();
	PlaneRange();
	LazySymbols();
	NumericFunctions();
	ContextFunctions();
	RefinementFunctions();
	RejectedSyntax();
	NumericFailures();
	Bounds();
	TokenCorpus();
	Ownership();
	std::printf("Numeric formula: %zu checks passed\n", checks);
	return 0;
}
