// Unit test for util/ExprEval.h's EvaluateExpression: ordinary arithmetic,
// functions/constants, the error paths, and - the actual reason this file
// exists - a bounded recursion depth. ParseUnary used to recurse once per
// nesting level with no limit at all for three constructs (parenthesized
// sub-expressions, a run of unary +/-, and a function call's own argument
// list), so a short (well under 100 KB), easily-constructed expression -
// 50,000 '(' characters, or 50,000 '-' characters - reliably segfaulted
// via stack overflow (confirmed directly before the depth guard existed:
// 50,000 nested parens crashed, 10,000 did not). This is reachable from
// untrusted input: a Flow "Expression" node's text comes straight from a
// loaded .dflow file (flow/FlowEditor.cpp) and is evaluated automatically
// the moment such a node loads - no user action beyond opening the file.
#include <cmath>
#include <cstdio>
#include <string>

#include "util/ExprEval.h"

using dino8::util::EvaluateExpression;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}
bool Close(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }
}  // namespace

int main() {
  // ---- Ordinary arithmetic, precedence, and grouping --------------------
  double v = 0;
  std::string err;
  Check(EvaluateExpression("2+3*4", v, err) && Close(v, 14), "precedence: 2+3*4 == 14");
  Check(EvaluateExpression("(2+3)*4", v, err) && Close(v, 20), "grouping: (2+3)*4 == 20");
  Check(EvaluateExpression("2^3^2", v, err) && Close(v, 512), "right-associative power: 2^3^2 == 2^(3^2) == 512");
  Check(EvaluateExpression("-5+3", v, err) && Close(v, -2), "leading unary minus");
  Check(EvaluateExpression("--5", v, err) && Close(v, 5), "double unary minus cancels");
  Check(EvaluateExpression("10%3", v, err) && Close(v, 1), "modulo");
  Check(EvaluateExpression("sqrt(16)", v, err) && Close(v, 4), "sqrt function");
  Check(EvaluateExpression("sin(90)", v, err) && Close(v, 1, 1e-6), "sin takes degrees");
  Check(EvaluateExpression("max(3,7)", v, err) && Close(v, 7), "two-argument function");
  Check(EvaluateExpression("pi", v, err) && Close(v, 3.14159265358979323846, 1e-12), "pi constant");

  // ---- Error paths --------------------------------------------------------
  Check(!EvaluateExpression("1/0", v, err), "division by zero is rejected, not Inf");
  Check(!EvaluateExpression("1+", v, err), "a truncated expression is rejected");
  Check(!EvaluateExpression("(1+2", v, err), "a missing ')' is rejected");
  Check(!EvaluateExpression("foo(1)", v, err), "an unknown function is rejected");
  Check(!EvaluateExpression("bar", v, err), "an unknown symbol is rejected");

  // ---- The actual regression: bounded recursion depth --------------------
  // A handful of nested parens still works (well under any real limit).
  {
    std::string expr(50, '(');
    expr += "5";
    expr += std::string(50, ')');
    Check(EvaluateExpression(expr, v, err) && Close(v, 5), "50 nested parens still evaluates normally");
  }
  // 50,000 - comfortably past the ~10,000-50,000 range that segfaulted
  // before the depth guard - must be REJECTED, not crash the process.
  {
    std::string expr(50000, '(');
    expr += "5";
    expr += std::string(50000, ')');
    Check(!EvaluateExpression(expr, v, err), "50,000 nested parens is rejected, not a stack overflow");
  }
  {
    std::string expr(50000, '-');
    expr += "5";
    Check(!EvaluateExpression(expr, v, err), "50,000 consecutive unary minuses is rejected, not a stack overflow");
  }
  {
    // Deep nesting inside a function call's own argument list - the third
    // of the three constructs that funnel through ParseUnary.
    std::string expr = "sqrt(" + std::string(50000, '(') + "5" + std::string(50000, ')') + ")";
    Check(!EvaluateExpression(expr, v, err), "deep nesting inside a function argument is rejected, not a stack overflow");
  }

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
