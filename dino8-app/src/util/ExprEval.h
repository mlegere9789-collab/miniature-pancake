// A tiny, dependency-free recursive-descent arithmetic expression
// evaluator - the same syntax Rhino-style numeric input fields accept
// ("2*(3+4)/5", sqrt/sin/cos/..., pi/e) - shared by the calculator/numeric-
// field evaluator (ui/Panels.cpp) and the Flow "Expression" node
// (flow/FlowEditor.cpp, where the expression text comes straight from a
// loaded .dflow file - untrusted input).
#pragma once

#include <string>

namespace dino8::util {

// Evaluates `text` as an arithmetic expression. Returns false and sets
// `error` on a malformed expression (unknown symbol, mismatched
// parenthesis, division by zero, or an expression nested deeper than
// kMaxExpressionDepth - see ExprEval.cpp's own doc comment on why that
// last one exists).
bool EvaluateExpression(const std::string& text, double& out, std::string& error);

}  // namespace dino8::util
