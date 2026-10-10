#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <string>

namespace LinuxBootstrap
{
/** Independent scalar bindings; false means unknown symbol or unavailable context. */
using NumericSymbolResolver = std::function<bool(const std::string&, double&)>;

/** Checked failures, never an implicit numeric zero or a parser exception. */
enum class NumericFormulaError
{
	None,
	EmptyExpression,
	LimitExceeded,
	UnsupportedSyntax,
	InvalidExpression,
	InvalidNumber,
	MissingResolver,
	UnknownSymbol,
	NonFiniteValue,
	ResolverFailure,
	InternalError
};

/** A value is usable only when Succeeded(); failures carry NaN, not a default. */
struct NumericFormulaResult
{
	NumericFormulaError error = NumericFormulaError::InternalError;
	float value = std::numeric_limits<float>::quiet_NaN();

	bool Succeeded() const noexcept { return error == NumericFormulaError::None; }
};

inline constexpr std::size_t NumericFormulaMaxLength = 4096;
inline constexpr std::size_t NumericFormulaMaxTokens = 512;
inline constexpr std::size_t NumericFormulaMaxNesting = 32;
inline constexpr std::size_t NumericFormulaMaxSymbols = 64;
inline constexpr std::size_t NumericFormulaMaxIdentifier = 64;

/**
 * @brief Evaluate a bounded numeric subset with muParser, without engine dependencies.
 *
 * Accepts decimal/scientific literals (optional adjacent f/F suffix), ASCII names
 * [A-Za-z_][A-Za-z_0-9]*, parentheses, unary +/- and binary + - * / < <= > >= == !=,
 * and lazy ?: with C++ operator precedence. All scalars use floating-point
 * arithmetic: 1/2 is 0.5, not C++ integer division. Unsuffixed literals and
 * arithmetic use double; f/F literals round to float first. The finite final
 * result is narrowed to float. This is NOT the full C++/game formula language or
 * a guarantee of bit-identical Windows floating-point execution.
 *
 * No functions, assignment, strings, lists, logical/bitwise/power operators,
 * comments, casts, hex/octal literals, or implicit built-in constants. Whitespace
 * is ASCII space, tab, CR and LF. Repeated unary signs require parentheses:
 * -(-2) is supported, - -2 is not. The lexer validates tokens; muParser owns the
 * grammar. Both parenthesis depth and total conditional count are capped at
 * MaxNesting; consecutive sign tokens have the same conservative cap.
 *
 * Each selected symbol is resolved at most once per call, only when its branch
 * executes. Unselected symbols need no context; malformed/unsupported syntax
 * in any branch still fails. A missing resolver is valid for literal-only or
 * unselected-symbol expressions. Resolvers must write a finite value on success.
 * Nonfinite executed arithmetic (even if later masked) fails. Every call owns its
 * parser/bindings; no results, callbacks or pointers survive the call. Resolver
 * exceptions are contained. Callers must provide thread-safe resolver state if
 * sharing it; no engine/default-value policy is implemented here.
 *
 * Requires C++17 floating-point from_chars, exceptions, and no fast-math. Build
 * the unmodified vendored sources with MUPARSER_STATIC and without OpenMP.
 */
NumericFormulaResult EvaluateNumericFormula(const std::string& expression,
	const NumericSymbolResolver& resolver = {}) noexcept;
}
