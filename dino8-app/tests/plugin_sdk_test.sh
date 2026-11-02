#!/usr/bin/env bash
# Proves the third-party plug-in path end to end, OUTSIDE the source tree:
#   1. install the SDK (plugin-sdk/) to a temp prefix; it must contain only the
#      ABI header + the CMake package files,
#   2. generate each plugin-templates/ template into a clean temp directory and
#      configure + build it against that prefix with nothing else,
#   3. load the three built plug-ins into the real app (headless) and check that
#      each plug-in's command ran,
#   4. the ABI compatibility rule (a plug-in built for a NEWER API declines
#      politely; one built for an older API loads),
#   5. package a plug-in, publish it through a localhost-only HTTP server as a
#      marketplace index, install it with PluginMarketplaceInstall and verify it.
# Prints "ok   psdk_..." / "FAIL psdk_..." lines; exits non-zero if any FAIL.
# Usage: bash plugin_sdk_test.sh <path to Dino8 binary>
trap '' PIPE
set -euo pipefail
echo() { builtin echo "$@" 2>/dev/null || true; }

HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$(cd "$HERE/.." && pwd)"
BIN="${1:?usage: plugin_sdk_test.sh <Dino8 binary>}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
W="$(mktemp -d)"
HTTP_PID=""
cleanup() { [ -n "$HTTP_PID" ] && kill "$HTTP_PID" 2>/dev/null || true; rm -rf "$W"; }
trap cleanup EXIT
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
# has <text> <fixed string> <check name>
has() { if grep -qF -- "$2" <<<"$1"; then ok "$3"; else bad "$3"; builtin echo "   wanted: $2" >&2; tail -8 <<<"$1" >&2 || true; fi; }

case "$(uname -s)" in Darwin) LIBEXT=dylib ;; MINGW*|MSYS*|CYGWIN*) LIBEXT=dll ;; *) LIBEXT=so ;; esac
if ! command -v cmake >/dev/null 2>&1 || ! command -v python3 >/dev/null 2>&1; then
  echo "skip psdk_all (cmake or python3 not available)"; exit 0
fi

run_app() {  # run_app <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" HOME="$1" timeout 900 "$BIN" --smoke 30 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" HOME="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$2" 2>&1
  fi
}

# ---- 1. install the SDK ------------------------------------------------------
cmake -S "$APP/plugin-sdk" -B "$W/sdk-build" -DCMAKE_INSTALL_PREFIX="$W/prefix" >"$W/sdk-cfg.log" 2>&1 || { cat "$W/sdk-cfg.log"; bad psdk_sdk_configure; exit 1; }
cmake --install "$W/sdk-build" >"$W/sdk-inst.log" 2>&1 || { cat "$W/sdk-inst.log"; bad psdk_sdk_install; exit 1; }
FILES="$(cd "$W/prefix" && find . -type f | sort)"
NFILES="$(grep -c . <<<"$FILES" || true)"
NHDR="$(grep -c '\.h$' <<<"$FILES" || true)"
NOTHER="$(grep -vcE '\.h$|\.cmake$' <<<"$FILES" || true)"
if [ "$NFILES" = 4 ] && [ "$NHDR" = 1 ] && [ "$NOTHER" = 0 ]; then
  ok "psdk_sdk_install: the SDK prefix holds exactly 4 files - 1 header ($(grep '\.h$' <<<"$FILES")) + 3 CMake package files, no sources, no libraries"
else bad "psdk_sdk_install: unexpected SDK contents ($NFILES files, $NHDR headers, $NOTHER others): $FILES"; fi
if cmp -s "$APP/include/dino8_plugin.h" "$W/prefix/include/dino8-plugin-sdk/dino8_plugin.h"; then
  ok "psdk_sdk_header_identical: the installed header is byte-identical to include/dino8_plugin.h (single source of truth)"
else bad "psdk_sdk_header_identical"; fi

# find_package version rule: 2 is satisfied, 99 is not (AnyNewerVersion: newer SDK satisfies older request)
mkdir -p "$W/fp-ok" "$W/fp-new"
printf 'cmake_minimum_required(VERSION 3.16)\nproject(fp LANGUAGES NONE)\nfind_package(Dino8PluginSdk 2 REQUIRED)\nmessage(STATUS "SDKAPI=${Dino8PluginSdk_API_VERSION}")\n' > "$W/fp-ok/CMakeLists.txt"
printf 'cmake_minimum_required(VERSION 3.16)\nproject(fp LANGUAGES NONE)\nfind_package(Dino8PluginSdk 99 REQUIRED)\n' > "$W/fp-new/CMakeLists.txt"
FPOK="$(cmake -S "$W/fp-ok" -B "$W/fp-ok/b" -DCMAKE_PREFIX_PATH="$W/prefix" 2>&1)" && has "$FPOK" "SDKAPI=2" "psdk_find_package_ok: find_package(Dino8PluginSdk 2) succeeds and reports API version 2" || bad "psdk_find_package_ok"
if FPNEW="$(cmake -S "$W/fp-new" -B "$W/fp-new/b" -DCMAKE_PREFIX_PATH="$W/prefix" 2>&1)"; then bad "psdk_find_package_too_new: find_package(Dino8PluginSdk 99) unexpectedly succeeded"
else ok "psdk_find_package_too_new: find_package(Dino8PluginSdk 99) is refused (a v2 SDK cannot satisfy a v99 request)"; fi

# ---- 2. generate + build each template, from clean directories -----------------
GEN="$W/gen"; mkdir -p "$GEN"
PLUG="$W/plugins"; mkdir -p "$PLUG"
gen_build() {  # gen_build <template> <slug> <command> <name> <prefix>
  local tmpl="$1" slug="$2" cmd="$3" name="$4" prefix="$5"
  python3 -I "$APP/plugin-templates/new_plugin.py" "$tmpl" --out "$GEN" --set plugin_slug="$slug" --set command_name="$cmd" \
    --set plugin_name="$name" --set author="Test Author" >"$W/$slug.gen.log" 2>&1 || { cat "$W/$slug.gen.log"; return 1; }
  # a clean directory: configure and build with cwd = a fresh empty dir, source = the generated project only
  local cwd="$W/cwd-$slug"; mkdir -p "$cwd"
  ( cd "$cwd" && cmake -S "$GEN/$slug" -B "$cwd/build" -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_BUILD_TYPE=Release >"$W/$slug.cfg.log" 2>&1 \
      && cmake --build "$cwd/build" >"$W/$slug.build.log" 2>&1 ) || { cat "$W/$slug.cfg.log" "$W/$slug.build.log" 2>/dev/null; return 1; }
  [ -f "$cwd/build/$slug.$LIBEXT" ]
}
build_one() {  # build_one <check prefix> <template> <slug> <command> <name>
  if gen_build "$2" "$3" "$4" "$5" "$W/prefix"; then
    cp "$W/cwd-$3/build/$3.$LIBEXT" "$PLUG/"
    ok "$1_builds: template '$2' generated into a clean dir and built against the SDK prefix alone -> $3.$LIBEXT"
  else bad "$1_builds: template '$2' did not build"; fi
}
build_one psdk_cmd command psdk_cmd PsdkHello "Psdk Hello"
build_one psdk_xyz import-export psdk_xyz PsdkXyz "Psdk Xyz Exchange"
build_one psdk_rep panel psdk_rep PsdkReport "Psdk Report Panel"

# no template or generated project may reference the Dino 8 source tree
LEAK="$(grep -rIlE "dino8-app|miniature-pancake|$APP" --exclude=README.md "$GEN" || true)"
if [ -z "$LEAK" ]; then ok "psdk_templates_self_contained: no generated source/CMake/manifest file mentions the Dino 8 source tree path"; else bad "psdk_templates_self_contained: source-tree path found in: $LEAK"; fi
# no unrendered placeholder survives
LEFT="$(grep -rIl 'cookiecutter\.' "$GEN" || true)"
if [ -z "$LEFT" ]; then ok "psdk_templates_rendered: no {{cookiecutter.*}} placeholder survives generation"; else bad "psdk_templates_rendered: unrendered placeholders in $LEFT"; fi
# without the SDK prefix the build must fail on find_package, naming the package
if NOSDK="$(cd "$W" && cmake -S "$GEN/psdk_cmd" -B "$W/nosdk-build" -DCMAKE_PREFIX_PATH="$W/does-not-exist" 2>&1)"; then bad "psdk_template_needs_sdk: configured without the SDK"
else has "$NOSDK" "Dino8PluginSdk" "psdk_template_needs_sdk: without the SDK prefix configuration fails and names the Dino8PluginSdk package"; fi

# ---- 4a. ABI compatibility builds (a v3-claiming and a v1-claiming copy of the SDK header) ----
for v in 3 1; do
  cp -R "$W/prefix" "$W/prefix_v$v"
  sed -i.bak "s/^#define DINO8_PLUGIN_API_VERSION [0-9]*/#define DINO8_PLUGIN_API_VERSION $v/" "$W/prefix_v$v/include/dino8-plugin-sdk/dino8_plugin.h"
  rm -f "$W/prefix_v$v/include/dino8-plugin-sdk/dino8_plugin.h.bak"
done
PLUG_ABI="$W/plugins_abi"; mkdir -p "$PLUG_ABI"
if gen_build command psdk_newer PsdkNewer "Psdk Newer Api" "$W/prefix_v3"; then cp "$W/cwd-psdk_newer/build/psdk_newer.$LIBEXT" "$PLUG_ABI/"; ok "psdk_abi_newer_builds: a plug-in built against an API v3 header builds"; else bad "psdk_abi_newer_builds"; fi
if gen_build command psdk_older PsdkOlder "Psdk Older Api" "$W/prefix_v1"; then cp "$W/cwd-psdk_older/build/psdk_older.$LIBEXT" "$PLUG_ABI/"; ok "psdk_abi_older_builds: a plug-in built against an API v1 header builds"; else bad "psdk_abi_older_builds"; fi

# ---- 3. load into the app headless ------------------------------------------------
mkdir -p "$W/cfg1"
printf '1 2 3\n# comment line\n4 5 6\n7 8 9\nnot a point\n' > "$W/in.txt"
cat > "$W/run1.txt" <<EOS
GrasshopperFolders add $PLUG
GrasshopperFolders add $PLUG_ABI
GrasshopperPluginList
PsdkHello 1 2 3
SelAll
PsdkXyzExport $W/export.txt
Delete
PsdkXyzImport $W/in.txt
Box 0,0,0 20,20,0 10
SelLast
PsdkReport
PsdkOlder 9 9 9
expect_objects 5
EOS
OUT1="$(run_app "$W/cfg1" "$W/run1.txt")" || { echo "$OUT1" | tail -30; bad "psdk_app_run: the app exited non-zero"; }
has "$OUT1" "Psdk Hello 1.0.0 - 1 command(s), 0 node(s)" "psdk_cmd_loaded: the command template's plug-in loaded in the app with its name, version and 1 command"
has "$OUT1" "Psdk Xyz Exchange 1.0.0 - 2 command(s), 0 node(s)" "psdk_xyz_loaded: the import/export template's plug-in loaded with 2 commands"
has "$OUT1" "Psdk Report Panel 1.0.0 - 1 command(s), 1 node(s)" "psdk_rep_loaded: the panel template's plug-in loaded with 1 command and 1 Dino Flow node"
has "$OUT1" "psdk_cmd: PsdkHello placed a point at (1, 2, 3)" "psdk_cmd_ran: the command plug-in's command ran and printed its result"
has "$OUT1" "psdk_xyz: exported 1 object(s) to $W/export.txt" "psdk_xyz_export_ran: the export command ran"
EXP="$(cat "$W/export.txt" 2>/dev/null || true)"
has "$EXP" "1 2 3" "psdk_xyz_export_file: the exported file holds the point's coordinates (1 2 3)"
has "$OUT1" "psdk_xyz: imported 3 point(s) from $W/in.txt" "psdk_xyz_import_ran: the import command read 3 points (comment and junk lines skipped)"
has "$OUT1" "size 20 x 20 x 10  box volume 4000" "psdk_rep_ran: the panel plug-in reported the selected box (20 x 20 x 10, volume 4000)"
has "$OUT1" "psdk_rep: 1 object(s), total box volume 4000" "psdk_rep_total: the panel plug-in printed its totals line"
has "$OUT1" "psdk_older: PsdkOlder placed a point at (9, 9, 9)" "psdk_abi_older_ran: and the API v1 plug-in's command ran"
has "$OUT1" "objects=5 " "psdk_app_objects: the plug-ins created exactly the expected 5 objects (3 imported points + the box + the API v1 plug-in's point)"
has "$OUT1" "Psdk Newer Api 1.0.0 - failed: dino8_plugin_init returned 2" "psdk_abi_newer_declined: a plug-in built for API v3 declines to load on this API v2 host (init returned 2), not crash"
has "$OUT1" "psdk_newer: needs plug-in API v3, this Dino 8 offers v2" "psdk_abi_newer_message: and says why in the command history"
has "$OUT1" "Psdk Older Api 1.0.0 - 1 command(s), 0 node(s)" "psdk_abi_older_loads: a plug-in built for API v1 loads on this API v2 host (additive ABI)"

# ---- 5. package + localhost marketplace round trip --------------------------------
if command -v curl >/dev/null 2>&1; then
  WWW="$W/www"; mkdir -p "$WWW"
  python3 -I "$APP/plugin-templates/package_plugin.py" --lib "$PLUG/psdk_cmd.$LIBEXT" --manifest "$GEN/psdk_cmd/manifest.json" \
    --url "http://127.0.0.1:PORT/psdk_cmd.$LIBEXT" --out "$W/dist" --tags "test,command" >"$W/pkg.log" 2>&1 || { cat "$W/pkg.log"; bad psdk_package; }
  [ -f "$W/dist/psdk_cmd-1.0.0/manifest.json" ] && [ -f "$W/dist/psdk_cmd-1.0.0/psdk_cmd.$LIBEXT" ] && [ -f "$W/dist/psdk_cmd-1.0.0.index-entry.json" ] \
    && ok "psdk_package_layout: package_plugin.py wrote psdk_cmd-1.0.0/{manifest.json,psdk_cmd.$LIBEXT} and an index entry" || bad "psdk_package_layout"
  cp "$PLUG/psdk_cmd.$LIBEXT" "$WWW/"
  PORT="$(python3 -I -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')"
  ( cd "$WWW" && exec python3 -I -m http.server "$PORT" --bind 127.0.0.1 >"$W/http.log" 2>&1 ) &
  HTTP_PID=$!
  python3 -I - "$W" "$PORT" "$LIBEXT" <<'PYEOF'
import json, sys
w, port, ext = sys.argv[1:4]
e = json.load(open(w + "/dist/psdk_cmd-1.0.0.index-entry.json"))
e["download_url"] = "http://127.0.0.1:%s/psdk_cmd.%s" % (port, ext)
bad = dict(e); bad["id"] = "psdk_tampered"; bad["name"] = "Psdk Tampered"; bad["sha256"] = "0" * 64
bad["library_filename"] = "psdk_tampered." + ext
idx = {"schema_version": 1, "index_name": "Psdk Localhost Index", "plugins": [e, bad]}
json.dump(idx, open(w + "/www/index.json", "w"), indent=1)
PYEOF
  for _ in 1 2 3 4 5 6 7 8 9 10; do curl -sf --noproxy '*' "http://127.0.0.1:$PORT/index.json" >/dev/null 2>&1 && break; sleep 0.3; done
  mkdir -p "$W/cfg2"
  cat > "$W/run2.txt" <<EOS
PluginMarketplaceIndex http://127.0.0.1:$PORT/index.json
PluginMarketplaceList
PluginMarketplaceInstall psdk_tampered
PluginMarketplaceInstall psdk_cmd
PsdkHello 4 5 6
PluginMarketplaceVerify psdk_cmd
expect_objects 1
EOS
  export no_proxy="127.0.0.1,localhost" NO_PROXY="127.0.0.1,localhost"; unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY all_proxy ALL_PROXY 2>/dev/null || true
  OUT2="$(run_app "$W/cfg2" "$W/run2.txt")" || { echo "$OUT2" | tail -20; bad "psdk_market_run: the app exited non-zero"; }
  has "$OUT2" "PluginMarketplaceIndex: loaded \"Psdk Localhost Index\" - 2 plug-in(s)" "psdk_market_index: the app loaded a third party's index over HTTP"
  has "$OUT2" "! PluginMarketplaceInstall: Psdk Tampered: sha256 mismatch" "psdk_market_sha_refused: an entry whose sha256 does not match the downloaded file is refused"
  has "$OUT2" "PluginMarketplaceInstall: installed psdk_cmd - 1 command(s), 0 flow node(s) registered" "psdk_market_install: the packaged plug-in installed from the localhost index"
  has "$OUT2" "psdk_cmd: PsdkHello placed a point at (4, 5, 6)" "psdk_market_ran: and its command ran"
  has "$OUT2" "PluginMarketplaceVerify: Psdk Hello (psdk_cmd): sha256 matches" "psdk_market_verify: PluginMarketplaceVerify re-hashed the installed copy and it matches the index sha256"
  kill "$HTTP_PID" 2>/dev/null || true; HTTP_PID=""
else
  echo "skip psdk_market (curl not available)"
fi


# ---- 6. signing: what the app really supports (DigitalSign / VerifySignature on any file) ----
# The app does NOT check signatures when it loads or installs a plug-in; it can sign and verify a
# file (e.g. a manifest.json) with a per-machine RSA-2048 key and a .sig sidecar. Prove exactly that.
SIGNED="$W/dist/psdk_cmd-1.0.0/manifest.json"
mkdir -p "$W/cfg3"
printf 'DigitalSign %s\nVerifySignature %s\n' "$SIGNED" "$SIGNED" > "$W/run3.txt"
OUT3="$(run_app "$W/cfg3" "$W/run3.txt")" || true
has "$OUT3" "VerifySignature: OK, signed by key fingerprint" "psdk_sign_verify: DigitalSign wrote manifest.json.sig and VerifySignature accepts the untouched manifest"
[ -f "$SIGNED.sig" ] && ok "psdk_sign_sidecar: the .sig sidecar exists next to the manifest" || bad "psdk_sign_sidecar"
printf '  \n' >> "$SIGNED"
printf 'VerifySignature %s\n' "$SIGNED" > "$W/run4.txt"
OUT4="$(run_app "$W/cfg3" "$W/run4.txt")" || true
has "$OUT4" "VerifySignature: FAILED" "psdk_sign_tamper: after the manifest is edited VerifySignature reports FAILED"

exit $fail
