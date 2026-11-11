# ---------------------------------------------------------------------------
# PKG_: packages (a folder with manifest.json + .lua / .dflow files) install from the command line and their scripts become commands.
# Before this the Package Manager panel copied .lua / .dflow files into <config>/plugins where nothing ever loaded them (only shared
# libraries are loaded from there), and there was no scriptable install / list / remove. Now PackageInstall <folder> [name] puts the
# package under <config>/packages/<name>, each script it exposes (manifest "commands": [{name, script}], or one command per script
# named after its file stem) is registered as a command at once and at every start-up, PackageList / PackageRemove list and delete,
# a package whose name is not a plain token ("../evil") is refused, and a command named like a built-in ("Line") is refused.
# Four throwaway packages are made here; expected values are known from construction, not read from the app:
#   pkgdemo  : PkgHello (hello.lua prints PKG_LUA_RAN <6*7 = 42>), PkgFlow (line.dflow = tests/flow_graph.dflow: slider 30 -> point
#              (30,5,0) -> line from the origin, so GrasshopperPlayer solves 4 nodes and the baked line is sqrt(30^2+5^2) = 30.41 long)
#   pkgplain : no "commands" key -> one command per script, plainone (prints PKG_PLAIN_RAN <3*5+1 = 16>)
#   pkgclash : declares a command "Line": refused, so the built-in Line still draws a 3-4-5 line of length 5
#   evilpkg  : manifest name "../evil": refused, and nothing is written outside <config>/packages
#   launch 1 (fresh config) installs all four (3 succeed), runs the commands, removes pkgplain (its command then says its package is
#            gone), pkgclash, a missing and an unsafe name; launch 2 (new process, same config) still has pkgdemo's commands with no
#            install step, plainone is gone, and after the source is edited (version 1.3.0, hello.lua prints 6*7+1 = 43) a named
#            reinstall replaces the files and PkgHello prints 43
# ---------------------------------------------------------------------------
pkg_ok() { echo "ok   $1"; }
pkg_bad() { echo "FAIL $1"; fail=1; }
if ! command -v python3 >/dev/null 2>&1 || [ "$IS_WINDOWS" = 1 ]; then
  echo "skip PKG_all (needs python3; not run on Windows)"
else
  PKG_DIR="$TMPW/pkg"; PKG_SRC="$PKG_DIR/src"; PKG_CFG="$PKG_DIR/config"
  mkdir -p "$PKG_SRC/pkgdemo" "$PKG_SRC/pkgplain" "$PKG_SRC/pkgclash" "$PKG_SRC/evilpkg" "$PKG_CFG"
  printf '%s\n' '{"name":"pkgdemo","version":"1.2.3","author":"t","description":"d","commands":[{"name":"PkgHello","script":"hello.lua"},{"name":"PkgFlow","script":"line.dflow"}]}' > "$PKG_SRC/pkgdemo/manifest.json"
  printf '%s\n' 'print("PKG_LUA_RAN " .. (6 * 7))' > "$PKG_SRC/pkgdemo/hello.lua"
  cp "$HERE/flow_graph.dflow" "$PKG_SRC/pkgdemo/line.dflow"
  printf '%s\n' '{"name":"pkgplain","version":"0.1"}' > "$PKG_SRC/pkgplain/manifest.json"
  printf '%s\n' 'print("PKG_PLAIN_RAN " .. (3 * 5 + 1))' > "$PKG_SRC/pkgplain/plainone.lua"
  printf '%s\n' '{"name":"pkgclash","commands":[{"name":"Line","script":"c.lua"}]}' > "$PKG_SRC/pkgclash/manifest.json"
  printf '%s\n' 'print("PKG_CLASH_RAN")' > "$PKG_SRC/pkgclash/c.lua"
  printf '%s\n' '{"name":"../evil","version":"9"}' > "$PKG_SRC/evilpkg/manifest.json"
  printf '%s\n' 'print("PKG_EVIL_RAN")' > "$PKG_SRC/evilpkg/x.lua"
  cat > "$PKG_DIR/l1.txt" <<EOS
@wait 3
PKGBEGIN_empty
PackageList
PKGBEGIN_avail
PackageList $PKG_SRC
PKGBEGIN_install
PackageInstall $PKG_SRC
PKGBEGIN_list
PackageList
PKGBEGIN_hello
PkgHello
PKGBEGIN_plain
plainone
PKGBEGIN_flow
PkgFlow Bake=Yes
SelAll
Length
PKGBEGIN_clash
New
Line 0,0,0 3,4,0
SelAll
Length
PKGBEGIN_remove
PackageRemove pkgplain
plainone
PackageRemove pkgclash
PackageRemove nosuchpkg
PackageRemove ../evil
PKGBEGIN_end
PackageList
PKGBEGIN_done
EOS
  cat > "$PKG_DIR/l2.txt" <<EOS
@wait 3
PKGBEGIN_state
PackageList
PkgHello
PkgFlow Bake=Yes
plainone
PKGBEGIN_avail2
PackageList $PKG_SRC
PKGBEGIN_reinstall
PackageInstall $PKG_SRC pkgdemo
PkgHello
PackageList
PKGBEGIN_done
EOS
  pkg_run() {
    if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
      XDG_CONFIG_HOME="$PKG_CFG" HOME="$PKG_CFG" timeout 600 "$BIN" --smoke 40 --script "$1" 2>&1
    else
      XDG_CONFIG_HOME="$PKG_CFG" HOME="$PKG_CFG" timeout 600 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 40 --script "$1" 2>&1
    fi
  }
  PKG_L1="$(pkg_run "$PKG_DIR/l1.txt")" || { echo "$PKG_L1" | tail -20; pkg_bad "PKG_run1: app exited non-zero"; PKG_L1=""; }
  # edit the source package between the launches: a new version whose script prints a different number
  printf '%s\n' '{"name":"pkgdemo","version":"1.3.0","author":"t","description":"d","commands":[{"name":"PkgHello","script":"hello.lua"},{"name":"PkgFlow","script":"line.dflow"}]}' > "$PKG_SRC/pkgdemo/manifest.json"
  printf '%s\n' 'print("PKG_LUA_RAN " .. (6 * 7 + 1))' > "$PKG_SRC/pkgdemo/hello.lua"
  PKG_L2="$(pkg_run "$PKG_DIR/l2.txt")" || { echo "$PKG_L2" | tail -20; pkg_bad "PKG_run2: app exited non-zero"; PKG_L2=""; }

  pkg_block() { awk -v name="$2" '$0 ~ ("Unknown command: PKGBEGIN_" name "$") { on = 1; next } on && /Unknown command: PKGBEGIN_/ { exit } on { print }' <<<"${!1}"; }
  pkg_has() { if grep -qxF -- "history: $3" <<<"$(pkg_block "$1" "$2")"; then pkg_ok "$4"; else pkg_bad "$4"; echo "     expected line: history: $3"; pkg_block "$1" "$2" | head -4 | sed 's/^/     actual:        /'; fi; }
  pkg_hasin() { if grep -qF -- "$3" <<<"$(pkg_block "$1" "$2")"; then pkg_ok "$4"; else pkg_bad "$4"; echo "     expected text: $3"; fi; }
  pkg_hasnot() { if grep -qF -- "$3" <<<"$(pkg_block "$1" "$2")"; then pkg_bad "$4"; else pkg_ok "$4"; fi; }

  pkg_has PKG_L1 empty "PackageList: 0 installed package(s)" "PKG_empty: nothing is installed in a fresh config"
  pkg_has PKG_L1 avail "PackageList: 4 package(s) in $PKG_SRC" "PKG_avail: PackageList <folder> finds the four source packages"
  pkg_has PKG_L1 avail "  pkgdemo 1.2.3 [not installed]" "PKG_avail: ... and says pkgdemo is not installed"
  pkg_has PKG_L1 install "PackageInstall: installed pkgdemo 1.2.3 (2 script command(s))" "PKG_install: pkgdemo installs with its two manifest commands"
  pkg_has PKG_L1 install "PackageInstall: installed pkgplain 0.1 (1 script command(s))" "PKG_install: pkgplain installs with one command per script (no commands key)"
  pkg_hasin PKG_L1 install "Package: refused to register 'Line' (package pkgclash) - a built-in command with that name already exists" "PKG_install: a command named like a built-in is refused"
  pkg_has PKG_L1 install "! PackageInstall: evilpkg: invalid package name '../evil' (letters, digits, '_', '-' and '.' only, not starting with '.')" "PKG_install: an unsafe package name is refused"
  pkg_has PKG_L1 install "PackageInstall: installed 3 package(s)" "PKG_install: three of the four installed"
  if [ ! -e "$PKG_CFG/dino8/evil" ] && [ ! -e "$PKG_CFG/evil" ] && [ -f "$PKG_CFG/dino8/packages/pkgdemo/manifest.json" ] && [ -f "$PKG_CFG/dino8/packages/pkgdemo/hello.lua" ]; then
    pkg_ok "PKG_files: pkgdemo's files are under <config>/packages/pkgdemo and nothing was written for ../evil"
  else pkg_bad "PKG_files: unexpected package files under $PKG_CFG/dino8"; fi
  pkg_has PKG_L1 list "PackageList: 3 installed package(s)" "PKG_list: three installed packages"
  pkg_has PKG_L1 list "  pkgdemo 1.2.3 - commands: PkgHello, PkgFlow" "PKG_list: pkgdemo lists its two commands"
  pkg_has PKG_L1 list "  pkgplain 0.1 - commands: plainone" "PKG_list: pkgplain lists plainone (named after the script file)"
  pkg_has PKG_L1 hello "PKG_LUA_RAN 42" "PKG_lua: the installed Lua script ran as a command (6*7 = 42)"
  pkg_has PKG_L1 plain "PKG_PLAIN_RAN 16" "PKG_lua: the default-named script command ran (3*5+1 = 16)"
  pkg_hasin PKG_L1 flow "GrasshopperPlayer: solved 4 node(s)" "PKG_dflow: the installed .dflow graph ran as a command (4 nodes)"
  pkg_hasin PKG_L1 flow "Total length = 30.41 " "PKG_dflow: ... and, with Bake=Yes passed through, baked the 30.41-long line"
  pkg_hasnot PKG_L1 clash "PKG_CLASH_RAN" "PKG_clash: the built-in Line was not shadowed"
  pkg_hasin PKG_L1 clash "Total length = 5" "PKG_clash: the built-in Line still draws the 3-4-5 line (length 5)"
  pkg_has PKG_L1 remove "PackageRemove: removed pkgplain (its commands stop working now and are gone after a restart)" "PKG_remove: PackageRemove pkgplain"
  pkg_has PKG_L1 remove "! plainone: its package 'pkgplain' is not installed any more" "PKG_remove: its command says its package is gone"
  pkg_has PKG_L1 remove "! PackageRemove: no package named 'nosuchpkg' is installed" "PKG_remove: a missing package is reported"
  pkg_has PKG_L1 remove "! PackageRemove: invalid package name '../evil'" "PKG_remove: an unsafe name is refused"
  pkg_has PKG_L1 end "PackageList: 1 installed package(s)" "PKG_remove: one package (pkgdemo) is left"

  pkg_has PKG_L2 state "PackageList: 1 installed package(s)" "PKG_restart: a new process still has the installed package"
  pkg_has PKG_L2 state "  pkgdemo 1.2.3 - commands: PkgHello, PkgFlow" "PKG_restart: ... with its commands"
  pkg_has PKG_L2 state "PKG_LUA_RAN 42" "PKG_restart: PkgHello runs with no install step in this process"
  pkg_hasin PKG_L2 state "GrasshopperPlayer: solved 4 node(s)" "PKG_restart: PkgFlow runs too"
  pkg_has PKG_L2 state "Unknown command: plainone" "PKG_restart: the removed package's command is gone"
  pkg_has PKG_L2 avail2 "  pkgdemo 1.3.0 [installed]" "PKG_avail: a package whose name is installed is flagged [installed]"
  pkg_has PKG_L2 reinstall "PackageInstall: installed pkgdemo 1.3.0 (2 script command(s))" "PKG_reinstall: the named reinstall picks up the edited source (1.3.0)"
  pkg_has PKG_L2 reinstall "PKG_LUA_RAN 43" "PKG_reinstall: PkgHello now runs the new script (6*7+1 = 43)"
  pkg_has PKG_L2 reinstall "  pkgdemo 1.3.0 - commands: PkgHello, PkgFlow" "PKG_reinstall: PackageList shows the new version"
fi
