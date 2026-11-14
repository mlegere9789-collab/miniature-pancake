#!/usr/bin/env bash
# Syntax-only checks of the harness scripts. This does NOT prove they work in the real applications.
set -u
H="$(cd "$(dirname "$0")/../harness" && pwd)"
T="$(cd "$(dirname "$0")" && pwd)"
fail=0
run() { local label="$1"; shift; if "$@" >/dev/null 2>&1; then echo "ok    $label"; else echo "FAIL  $label"; fail=1; fi; }
for f in "$H/rhino/d8_reference_rhino.py" "$H/grasshopper/d8_reference_grasshopper.py" "$H/archicad/d8_reference_archicad.py" \
         "$H/revit/d8_reference_revit.py" "$H/blender/d8_reference_blender.py" "$H/forma/serve.py"; do
  run "python3 compile() $(basename "$f")" python3 -I -c "import sys; compile(open(sys.argv[1], encoding='utf-8').read(), sys.argv[1], 'exec')" "$f"
done
if command -v ruby >/dev/null; then run "ruby -c d8_reference_sketchup.rb" ruby -c "$H/sketchup/d8_reference_sketchup.rb"; else echo "skip  ruby not installed"; fi
if command -v node >/dev/null; then run "node --check harness.js" node --check "$H/forma/src/harness.js"; else echo "skip  node not installed"; fi
run "lisp parenthesis balance d8_reference_autocad.lsp" python3 -I "$T/lisp_paren_check.py" "$H/autocad/d8_reference_autocad.lsp"
# policy checks
if grep -rniE "(requests|urllib|http\.client|socket|curl|wget|WebClient|XMLHttpRequest|fetch\()" "$H" --include=*.py --include=*.rb --include=*.lsp --include=*.js -l | grep -v serve.py | grep -q .; then echo "NOTE  network-related words found (review):"; grep -rniE "(requests|urllib|http\.client|socket|curl|wget|WebClient|XMLHttpRequest|fetch\()" "$H" --include=*.py --include=*.rb --include=*.lsp --include=*.js | grep -v serve.py | head; fi
exit $fail
