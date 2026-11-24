# Grasshopper observed behaviour (Phase B; our own words; Rhino 8.36.26296.17001; two full passes identical, 264 cases)

Source: `golden/gh-rhino8.36.26296.17001-2026-11-23.jsonl`. Components are addressed by name through the public component registry; inputs are set as persistent data on branch {0}
unless stated. Paths in the notes are as the solver reports them.

## Sequences and lists
* A series with a negative step counts down (GH-SERI-0003: 5, 4, 3, 2). A zero step repeats the start (GH-SERI-0005). Count 0 gives an empty list (GH-SERI-0006); a negative count also gives an empty
  list and raises one warning (GH-SERI-0008).
* A range over a domain with n steps returns n + 1 values including both ends, in the direction of the domain; a reversed domain counts down (GH-RANG-0001, -0005). Zero steps gives an empty list (GH-RANG-0006).
* Shift with wrap moves the first item to the end; without wrap it drops it (GH-SHFT-0003, -0004).
* List item at an index past the end without wrap returns null and raises one warning (GH-LITM-0012); with wrap the index is taken modulo the length.

## Data matching (math operators)
* Unequal lists: the shorter list's last item repeats (GH-MATH-0002: [1,2,3] + [10,20] = [11,22,23]; GH-MATH-0003: [1,2] + [10,20,30,40] = [11,22,32,42]).
* An empty second list leaves the first unchanged for addition (GH-MATH-0005).
* Trees: matching is branch by branch by path; a single branch against several branches pairs with each (GH-TMTH-0001: {0},{1} against {0}); a single-item branch against
  several branches is broadcast, and the output keeps the longer tree's paths (GH-TMTH-0002).

## Trees
* Graft puts every item on its own new branch, appending an index to the path (GH-TREE-0007). Flatten collapses every branch into path {0} in input order (GH-TREE-0008).
* Simplify leaves a tree unchanged when its branches have no shared redundant prefix (GH-TREE-0006).

## Expression evaluator
* Operators and precedence: unary minus binds tighter than the power operator, so "-x^2" with x = 3 gives 9, not -9 (GH-EVAL-0015, -0016); a negative base to power 2 gives 9 (GH-EVAL-0012);
  zero to the power zero is 1 (GH-EVAL-0013).
* Division by zero is an error with a null result (GH-EVAL-0019, 1/0 in GH-EVAL-0073). The remainder operator takes the sign of the dividend (-3 % 2 = -1, GH-EVAL-0022) and 0 % 0 gives NaN with no error (GH-EVAL-0023).
* Comparison "x > y" yields a boolean (GH-EVAL-0057). The not-equal form is "<>" (GH-EVAL-0061) while "==" is an error (GH-EVAL-0059): equality uses a single "=".
* "if(x>y,1,2)" picks by condition (GH-EVAL-0055, -0056). The factorial postfix works for 3 (6) and errors for -3 (GH-EVAL-0081, -0082).
* "x&y" concatenates the two numbers as text ("32") (GH-EVAL-0083). Syntax errors and unknown functions give one error and a null result (GH-EVAL-0075, -0077).
* Floating-point sums are returned to double precision; our records keep 12 significant digits (GH-EVAL-0085).

## Geometry cross-checks
* Circle of radius r has length 2 pi r (GH-CIRC-0001: r = 0.5 gives 3.14159265359). Radius 0 yields a circle with length 0 and no warning (GH-CIRC-0005).

## Not yet covered
Tree path mapper and expression-based path ops, sort with multiple keys, geometry-producing components beyond circles, and all plug-ins. Next corpora should add these.
