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

/** FormulaPars f2l narrows to float then truncates toward zero; reject unsafe int casts. */
double FloatToInteger(double value)
{
	const float v = Narrow(value);
	if (double(v) < std::numeric_limits<int>::min() || double(v) > std::numeric_limits<int>::max())
		throw Error::NonFiniteValue;
	return static_cast<int>(v);
}

bool IsNumericFunction(const std::string& name)
{
	return name == "min" || name == "max" || name == "clamp" || name == "lerp" ||
		name == "round" || name == "abs" || name == "sqrt" || name == "floor" || name == "ceil" || name == "f2l";
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
/** Four reserved endpoint bindings are emitted only by the engine-context source whitelist. */
bool IsHistoryFunction(const std::string& name)
{
	return name == "__history_s_taken" || name == "__history_s_dealt" ||
		name == "__history_r_taken" || name == "__history_r_dealt";
}

/** Reserved context transport; authored member syntax is checked by formula_context. */
bool IsExtendedHistoryFunction(const std::string& name)
{
	return name == "__history_s_taken_ex" || name == "__history_s_dealt_ex" ||
		name == "__history_r_taken_ex" || name == "__history_r_dealt_ex" ||
		name == "__history_s_taken_ex_misc" || name == "__history_s_dealt_ex_misc" ||
		name == "__history_r_taken_ex_misc" || name == "__history_r_dealt_ex_misc";
}
double ResolveOne(void* data, double a)
{
	return ResolveFunction(*static_cast<FunctionBinding*>(data), &a, 1);
}
double ResolveTwo(void* data, double a, double b)
{
	const double args[] = {a, b};
	return ResolveFunction(*static_cast<FunctionBinding*>(data), args, 2);
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
	bool nativeTerrain;
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
	parser.DefineFun("f2l", FloatToInteger, false);
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
	bool pendingNativeTerrain = false;
	// Macro-injected getters share the same lazy snapshot and symbol limit as source names.
	const auto symbol = [&](const std::string& name) {
		auto binding = bindings.find(name);
		if (binding == bindings.end())
		{
			if (bindings.size() >= NumericFormulaMaxSymbols) throw Error::LimitExceeded;
			const std::string token = "pw_symbol_" + std::to_string(bindings.size());
			binding = bindings.emplace(name, Binding{&resolver, name, token}).first;
			parser.DefineFunUserData(token, Resolve, &binding->second, false);
		}
		return binding->second.token + "() ";
	};
	for (std::size_t pos = 0; pos < expression.size();)
	{
		const char ch = expression[pos];
		if (IsSpace(ch)) { ++pos; continue; }
		if (++tokens > NumericFormulaMaxTokens) throw Error::LimitExceeded;
		signs = ch == '+' || ch == '-' || ch == '!' ? signs + 1 : 0;
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
				if (name == "nt")
				{
					// FormulaPars is a two-argument ?: macro, NOT eager SwitchByBool.
					// The existing separator stack supplies argument parentheses; muParser
					// still validates/evaluates the resulting lazy conditional expression.
					if (++conditionals > NumericFormulaMaxNesting) throw Error::LimitExceeded;
					translated += "(" + symbol("sTerrainType") + "==" + symbol("sFctn") + "?";
					pendingNativeTerrain = true;
				}
				else if (name == "abilityScale" || name == "damageScale" || name == "refineScale" || name == "random" || name == "ut_bldg" || name == "ut_hero" || IsHistoryFunction(name) || IsExtendedHistoryFunction(name))
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
			translated += symbol(name);
			continue;
		}

		const char next = pos + 1 < expression.size() ? expression[pos + 1] : '\0';
		if ((ch == '&' && next == '&') || (ch == '|' && next == '|'))
		{
			if (++conditionals > NumericFormulaMaxNesting) throw Error::LimitExceeded;
			translated += expression.substr(pos, 2) + " ";
			pos += 2;
			continue;
		}
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
			functionParentheses.push_back({pendingFunction, pendingContext, pendingNativeTerrain});
			pendingFunction = false;
			pendingContext = nullptr;
			pendingNativeTerrain = false;
		}
		else if (ch == ')')
		{
			if (depth == 0) throw Error::InvalidExpression;
			--depth;
			const auto& frame = functionParentheses.back();
			if (frame.nativeTerrain)
			{
				if (frame.arguments != 2) throw Error::InvalidExpression;
				translated += ')';
			}
			if (frame.context)
			{
				if (IsHistoryFunction(frame.context->name))
				{
					if (frame.arguments != 1) throw Error::InvalidExpression;
					parser.DefineFunUserData(frame.context->token, ResolveOne, frame.context, false);
				}
				else if (frame.context->name == "random" || frame.context->name == "refineScale" || frame.context->name == "ut_bldg" || frame.context->name == "ut_hero" || IsExtendedHistoryFunction(frame.context->name))
				{
					if (frame.arguments != 2) throw Error::InvalidExpression;
					parser.DefineFunUserData(frame.context->token, ResolveTwo, frame.context, false);
				}
				else if (frame.arguments == 3)
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
			if (functionParentheses.back().nativeTerrain)
			{
				if (functionParentheses.back().arguments != 2) throw Error::InvalidExpression;
				translated += "):(";
				++pos;
				continue;
			}
		}
		else if (ch == '?')
		{
			if (++conditionals > NumericFormulaMaxNesting) throw Error::LimitExceeded;
		}
		else if (ch != ':' && ch != '+' && ch != '-' && ch != '*' && ch != '/' && ch != '<' && ch != '>' && ch != '!')
			throw Error::UnsupportedSyntax;
		translated += ch;
		translated += ' ';
		++pos;
	}
	if (tokens == 0) throw Error::EmptyExpression;
	if (depth != 0) throw Error::InvalidExpression;
	return translated;
}

/** Lower validated, bound logical syntax to muParser's lazy conditional bytecode.
 * This translates structure only: muParser still validates calls and evaluates all
 * arithmetic. Its eager logical opcodes cannot provide C++ callback short-circuiting.
 * Input length, tokens, nesting and conditional count are bounded by BindTokens.
 */
class LogicalLowering
{
public:
	explicit LogicalLowering(const std::string& bound)
	{
		for (std::size_t pos = 0; pos < bound.size();)
		{
			if (IsSpace(bound[pos])) { ++pos; continue; }
			const std::size_t begin = pos++;
			if (IsNameStart(bound[begin]))
				while (pos < bound.size() && (IsNameStart(bound[pos]) || IsDigit(bound[pos]))) ++pos;
			else if (pos < bound.size())
			{
				const std::string pair = bound.substr(begin, 2);
				if (pair == "&&" || pair == "||" || pair == "==" || pair == "!=" || pair == "<=" || pair == ">=") ++pos;
			}
			tokens.push_back(bound.substr(begin, pos - begin));
		}
	}

	std::string Translate()
	{
		const std::string result = Expression(1);
		if (position != tokens.size()) throw Error::InvalidExpression;
		return result;
	}

private:
	std::vector<std::string> tokens;
	std::size_t position = 0;

	bool Take(const char* token)
	{
		if (position == tokens.size() || tokens[position] != token) return false;
		++position;
		return true;
	}

	void Require(const char* token)
	{
		if (!Take(token)) throw Error::InvalidExpression;
	}

	static int Precedence(const std::string& op)
	{
		if (op == "||") return 2;
		if (op == "&&") return 3;
		if (op == "==" || op == "!=") return 4;
		if (op == "<" || op == "<=" || op == ">" || op == ">=") return 5;
		if (op == "+" || op == "-") return 6;
		if (op == "*" || op == "/") return 7;
		return 0;
	}

	std::string Operand()
	{
		if (position == tokens.size()) throw Error::InvalidExpression;
		const std::string token = tokens[position++];
		if (token == "!") return "(" + Operand() + "==0)";
		if (token == "+" || token == "-")
		{
			// Retain the numeric subset's parenthesis requirement for repeated unary signs.
			if (position != tokens.size() && (tokens[position] == "+" || tokens[position] == "-")) throw Error::InvalidExpression;
			return "(" + token + Operand() + ")";
		}
		if (token == "(")
		{
			const std::string result = Expression(1);
			Require(")");
			return "(" + result + ")";
		}
		if (!IsNameStart(token[0])) throw Error::InvalidExpression;
		if (!Take("(")) return token;
		std::string result = token + "(";
		if (!Take(")"))
		{
			result += Expression(1);
			while (Take(",")) result += "," + Expression(1);
			Require(")");
		}
		return result + ")";
	}

	std::string Expression(int minimum)
	{
		std::string left = Operand();
		while (position != tokens.size())
		{
			const std::string op = tokens[position];
			if (op == "?" && minimum <= 1)
			{
				++position;
				const std::string yes = Expression(1);
				Require(":");
				const std::string no = Expression(1);
				left = "(" + left + "?" + yes + ":" + no + ")";
				continue;
			}
			const int precedence = Precedence(op);
			if (precedence < minimum) break;
			++position;
			const std::string right = Expression(precedence + 1);
			if (op == "&&") left = "(" + left + "?(" + right + "!=0):0)";
			else if (op == "||") left = "(" + left + "?1:(" + right + "!=0))";
			else left = "(" + left + op + right + ")";
		}
		return left;
	}
};
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
		std::string bound = BindTokens(expression, resolver, functions, parser, bindings, calls);
		if (bound.find("&&") != std::string::npos || bound.find("||") != std::string::npos || bound.find("! ") != std::string::npos)
			bound = LogicalLowering(bound).Translate();
		parser.SetExpr(bound);
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
