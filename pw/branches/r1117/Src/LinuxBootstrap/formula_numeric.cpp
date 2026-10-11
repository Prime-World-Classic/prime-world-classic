#include "formula_numeric.h"

#include "third_party/muparser/include/muParser.h"

#include <charconv>
#include <cmath>
#include <map>
#include <system_error>
#include <vector>

#if defined(__FAST_MATH__)
#error Numeric formula checks require finite-aware arithmetic, not fast-math.
#endif

namespace LinuxBootstrap
{
namespace
{
using Error = NumericFormulaError;

bool IsSpace(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool IsDigit(char ch)
{
	return ch >= '0' && ch <= '9';
}

bool IsNameStart(char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

/** Check before conversion: an out-of-range floating conversion is not our policy. */
float Narrow(double value)
{
	if (!std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max())
		throw Error::NonFiniteValue;
	return static_cast<float>(value);
}

double Finite(double value)
{
	if (!std::isfinite(value)) throw Error::NonFiniteValue;
	return value;
}

/** Reject executed invalid intermediates, even when a comparison would hide them. */
double Add(double a, double b) { return Finite(a + b); }
double Subtract(double a, double b) { return Finite(a - b); }
double Multiply(double a, double b) { return Finite(a * b); }
double Divide(double a, double b)
{
	if (b == 0.0) throw Error::NonFiniteValue;
	return Finite(a / b);
}
double Positive(double value) { return value; }
double Negative(double value) { return -value; }
double Less(double a, double b) { return a < b; }
double LessEqual(double a, double b) { return a <= b; }
double Greater(double a, double b) { return a > b; }
double GreaterEqual(double a, double b) { return a >= b; }
double Equal(double a, double b) { return a == b; }
double NotEqual(double a, double b) { return a != b; }

/** Authored FormulaPars helpers take float arguments, including parameter-first lerp. */
double Minimum(double a, double b)
{
	const float x = Narrow(a), y = Narrow(b);
	return x < y ? x : y;
}
double Maximum(double a, double b)
{
	const float x = Narrow(a), y = Narrow(b);
	return x > y ? x : y;
}
double Clamp(double value, double low, double high) { return Minimum(Maximum(value, low), high); }
double Lerp(double parameter, double low, double high)
{
	const float p = Narrow(parameter), a = Narrow(low), b = Narrow(high);
	const float inverse = Narrow(Finite(1.0f - p));
	const float left = Narrow(Finite(a * inverse)), right = Narrow(Finite(b * p));
	return Finite(left + right);
}
double Round(double value)
{
	const float v = Narrow(value);
	return v >= 0.0f ? std::floor(v + 0.5f) : std::ceil(v - 0.5f);
}
double Absolute(double value) { return Finite(std::fabs(value)); }
double SquareRoot(double value) { return Finite(std::sqrt(value)); }
double Floor(double value) { return Finite(std::floor(value)); }
double Ceil(double value) { return Finite(std::ceil(value)); }

bool IsNumericFunction(const std::string& name)
{
	return name == "min" || name == "max" || name == "clamp" || name == "lerp" ||
		name == "round" || name == "abs" || name == "sqrt" || name == "floor" || name == "ceil";
}

/** Stable map entries are callback userdata, owned only by the current evaluation. */
struct Binding
{
	const NumericSymbolResolver* resolver;
	std::string name;
	std::string token;
	double value = std::numeric_limits<double>::quiet_NaN();
	bool resolved = false;
};

/** Non-optimizable callbacks preserve ?: laziness and read one snapshot per symbol. */
double Resolve(void* data)
{
	auto& binding = *static_cast<Binding*>(data);
	if (binding.resolved) return binding.value;
	if (!*binding.resolver) throw Error::MissingResolver;
	bool found = false;
	try
	{
		found = (*binding.resolver)(binding.name, binding.value);
	}
	catch (...)
	{
		throw Error::ResolverFailure;
	}
	if (!found) throw Error::UnknownSymbol;
	Finite(binding.value);
	binding.resolved = true;
	return binding.value;
}

/** Stable per-call userdata never escapes this evaluation; callbacks remain lazy. */
struct FunctionBinding
{
	const NumericFunctionResolver* resolver;
	std::string name;
	std::string token;
};

double ResolveFunction(FunctionBinding& binding, const double* args, std::size_t count)
{
	if (!*binding.resolver) throw Error::MissingResolver;
	for (std::size_t i = 0; i < count; ++i) Finite(args[i]);
	double value = std::numeric_limits<double>::quiet_NaN();
	bool found = false;
	try { found = (*binding.resolver)(binding.name, args, count, value); }
	catch (...) { throw Error::ResolverFailure; }
	if (!found) throw Error::UnknownSymbol;
	return Finite(value);
}
double ResolveThree(void* data, double a, double b, double c)
{
	const double args[] = {a, b, c};
	return ResolveFunction(*static_cast<FunctionBinding*>(data), args, 3);
}
double ResolveFour(void* data, double a, double b, double c, double d)
{
	const double args[] = {a, b, c, d};
	return ResolveFunction(*static_cast<FunctionBinding*>(data), args, 4);
}

/** Track separators only, leaving expression grammar and fixed arity to muParser. */
struct Parenthesis
{
	bool function;
	FunctionBinding* context;
	std::size_t arguments = 1;
};

/** Remove all ambient functionality; muParser still owns grammar and lazy branching. */
void Configure(mu::Parser& parser)
{
	parser.ClearFun();
	parser.ClearConst();
	parser.ClearInfixOprt();
	parser.ClearPostfixOprt();
	parser.ClearOprt();
	parser.EnableBuiltInOprt(false);
	parser.EnableOptimizer(false);
	// muParser groups equality/relations by default; C++ gives relations priority.
	parser.DefineOprt("==", Equal, 4);
	parser.DefineOprt("!=", NotEqual, 4);
	parser.DefineOprt("<", Less, 5);
	parser.DefineOprt("<=", LessEqual, 5);
	parser.DefineOprt(">", Greater, 5);
	parser.DefineOprt(">=", GreaterEqual, 5);
	parser.DefineOprt("+", Add, 6);
	parser.DefineOprt("-", Subtract, 6);
	parser.DefineOprt("*", Multiply, 7);
	parser.DefineOprt("/", Divide, 7);
	parser.DefineInfixOprt("+", Positive, 8, false);
	parser.DefineInfixOprt("-", Negative, 8, false);
	parser.DefineConst("true", 1);
	parser.DefineConst("false", 0);
	parser.DefineFun("min", Minimum, false);
	parser.DefineFun("max", Maximum, false);
	parser.DefineFun("clamp", Clamp, false);
	parser.DefineFun("lerp", Lerp, false);
	parser.DefineFun("round", Round, false);
	parser.DefineFun("abs", Absolute, false);
	parser.DefineFun("sqrt", SquareRoot, false);
	parser.DefineFun("floor", Floor, false);
	parser.DefineFun("ceil", Ceil, false);
}

/**
 * Token validation/renaming only: no precedence, expression tree or evaluation here.
 * Numeric constants use locale-independent from_chars. Symbols become private
 * zero-argument callbacks. Only explicit helpers accept calls; commas are allowed
 * inside their argument parentheses, never as muParser expression-list operators.
 */
std::string BindTokens(const std::string& expression, const NumericSymbolResolver& resolver,
	const NumericFunctionResolver& functions, mu::Parser& parser,
	std::map<std::string, Binding>& bindings, std::map<std::string, FunctionBinding>& calls)
{
	if (expression.size() > NumericFormulaMaxLength) throw Error::LimitExceeded;
	std::string translated;
	std::size_t tokens = 0;
	std::size_t depth = 0;
	std::size_t conditionals = 0;
	std::size_t signs = 0;
	std::vector<Parenthesis> functionParentheses;
	bool pendingFunction = false;
	FunctionBinding* pendingContext = nullptr;
	for (std::size_t pos = 0; pos < expression.size();)
	{
		const char ch = expression[pos];
		if (IsSpace(ch)) { ++pos; continue; }
		if (++tokens > NumericFormulaMaxTokens) throw Error::LimitExceeded;
		signs = ch == '+' || ch == '-' ? signs + 1 : 0;
		if (signs > NumericFormulaMaxNesting) throw Error::LimitExceeded;

		if (IsDigit(ch) || ch == '.')
		{
			const char* begin = expression.data() + pos;
			const char* end = expression.data() + expression.size();
			double value = 0;
			const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
			if (parsed.ec != std::errc{} || parsed.ptr == begin) throw Error::InvalidNumber;
			const std::string number(begin, parsed.ptr);
			if (number.size() > 1 && ch == '0' && IsDigit(number[1]) &&
				number.find_first_of(".eE") == std::string::npos) throw Error::UnsupportedSyntax;
			pos = static_cast<std::size_t>(parsed.ptr - expression.data());
			if (pos < expression.size() && (expression[pos] == 'f' || expression[pos] == 'F'))
			{
				value = Narrow(value);
				++pos;
			}
			if (pos < expression.size() &&
				(IsNameStart(expression[pos]) || IsDigit(expression[pos]) || expression[pos] == '.'))
				throw Error::InvalidNumber;
			const std::string token = "pw_number_" + std::to_string(tokens);
			parser.DefineConst(token, Finite(value));
			translated += token + " ";
			continue;
		}

		if (IsNameStart(ch))
		{
			const std::size_t begin = pos++;
			while (pos < expression.size() && (IsNameStart(expression[pos]) || IsDigit(expression[pos]))) ++pos;
			if (pos - begin > NumericFormulaMaxIdentifier) throw Error::LimitExceeded;
			const std::string name = expression.substr(begin, pos - begin);
			if (name == "nullptr") throw Error::UnsupportedSyntax;
			std::size_t next = pos;
			while (next < expression.size() && IsSpace(expression[next])) ++next;
			if (next < expression.size() && expression[next] == '(')
			{
				if (name == "abilityScale" || name == "damageScale")
				{
					const std::string token = "pw_call_" + std::to_string(calls.size());
					pendingContext = &calls.emplace(token, FunctionBinding{&functions, name, token}).first->second;
					translated += token;
				}
				else
				{
					if (!IsNumericFunction(name)) throw Error::UnsupportedSyntax;
					// muParser requires the opening parenthesis adjacent to a function name.
					translated += name;
				}
				pendingFunction = true;
				continue;
			}
			if (name == "true" || name == "false")
			{
				translated += name + " ";
				continue;
			}
			auto binding = bindings.find(name);
			if (binding == bindings.end())
			{
				if (bindings.size() >= NumericFormulaMaxSymbols) throw Error::LimitExceeded;
				const std::string token = "pw_symbol_" + std::to_string(bindings.size());
				binding = bindings.emplace(name, Binding{&resolver, name, token}).first;
				parser.DefineFunUserData(token, Resolve, &binding->second, false);
			}
			translated += binding->second.token + "() ";
			continue;
		}

		const char next = pos + 1 < expression.size() ? expression[pos + 1] : '\0';
		if ((ch == '<' || ch == '>' || ch == '=' || ch == '!') && next == '=')
		{
			translated += expression.substr(pos, 2) + " ";
			pos += 2;
			continue;
		}
		if ((ch == '+' && (next == '+' || next == '=')) ||
			(ch == '-' && (next == '-' || next == '=' || next == '>')) ||
			(ch == '*' && (next == '*' || next == '=' || next == '/')) ||
			(ch == '/' && (next == '/' || next == '*' || next == '=')) ||
			(ch == '<' && next == '<') || (ch == '>' && next == '>')) throw Error::UnsupportedSyntax;
		if (ch == '(')
		{
			if (++depth > NumericFormulaMaxNesting) throw Error::LimitExceeded;
			functionParentheses.push_back({pendingFunction, pendingContext});
			pendingFunction = false;
			pendingContext = nullptr;
		}
		else if (ch == ')')
		{
			if (depth == 0) throw Error::InvalidExpression;
			--depth;
			const auto& frame = functionParentheses.back();
			if (frame.context)
			{
				if (frame.arguments == 3)
					parser.DefineFunUserData(frame.context->token, ResolveThree, frame.context, false);
				else if (frame.arguments == 4)
					parser.DefineFunUserData(frame.context->token, ResolveFour, frame.context, false);
				else throw Error::InvalidExpression;
			}
			functionParentheses.pop_back();
		}
		else if (ch == ',')
		{
			if (functionParentheses.empty() || !functionParentheses.back().function) throw Error::UnsupportedSyntax;
			++functionParentheses.back().arguments;
		}
		else if (ch == '?')
		{
			if (++conditionals > NumericFormulaMaxNesting) throw Error::LimitExceeded;
		}
		else if (ch != ':' && ch != '+' && ch != '-' && ch != '*' && ch != '/' && ch != '<' && ch != '>')
			throw Error::UnsupportedSyntax;
		translated += ch;
		translated += ' ';
		++pos;
	}
	if (tokens == 0) throw Error::EmptyExpression;
	if (depth != 0) throw Error::InvalidExpression;
	return translated;
}
}

NumericFormulaResult EvaluateNumericFormula(const std::string& expression,
	const NumericSymbolResolver& resolver, const NumericFunctionResolver& functions) noexcept
{
	try
	{
		// Bindings outlive the parser and never escape this evaluation.
		std::map<std::string, Binding> bindings;
		std::map<std::string, FunctionBinding> calls;
		mu::Parser parser;
		Configure(parser);
		parser.SetExpr(BindTokens(expression, resolver, functions, parser, bindings, calls));
		const float value = Narrow(parser.Eval());
		return {Error::None, value};
	}
	catch (Error error)
	{
		return {error};
	}
	catch (const mu::Parser::exception_type&)
	{
		return {Error::InvalidExpression};
	}
	catch (...)
	{
		return {Error::InternalError};
	}
}
}
