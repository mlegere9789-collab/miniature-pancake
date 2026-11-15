# ---------------------------------------------------------------------------
# PLS_: plug-in manager settings persist, and plug-ins can be switched off and on.
# Before this, GrasshopperFolders / GrasshopperIgnorePlugin / GrasshopperLoadOneByOne changed in-memory lists that nothing wrote
# to settings.json (the header of plugins/PluginManager.h even said "persisted alongside app settings.json"), `enabled` on a loaded
# plug-in was never set by anything, and there was no way to un-ignore a plug-in. Now: settings.json carries a "plugins" object
# (folders, ignored, load_one_by_one), PlugInDisable / PlugInEnable gate a plug-in's commands at once and keep it from loading
# next session, GrasshopperIgnorePlugin <file> / GrasshopperFolders add are de-duplicated, and both have a `remove` form.
# Two throwaway plug-ins are compiled here with the system C compiler against include/dino8_plugin.h (the same public header a
# third-party plug-in uses): "PlsAlpha" (pls_alpha.so, command PlsA prints PLS_A_RAN) and "PlsBeta" (pls_beta.so, PlsB prints
# PLS_B_RAN). Expected values are known from construction, not read from the app:
#   launch 1 (fresh config): ignore pls_beta.so, add the folder twice (the 2nd is refused as a duplicate), toggle load-one-by-one on,
#            PlsA runs, PlugInDisable PlsAlpha -> PlsA refuses; PlsB is unknown (ignored). On exit settings.json must hold
#            folders ["<dir>"], ignored ["pls_beta.so","pls_alpha.so"], load_one_by_one true
#   launch 2 (same config, new process): the folder is still listed (1), 2 ignored, load-one-by-one toggles to "off" (it was on),
#            PlsA unknown (disabled plug-ins do not load); PlugInEnable pls_alpha loads it, PlsA runs; `GrasshopperIgnorePlugin
#            remove pls_beta` loads PlsB, PlsB runs; 0 ignored. On exit settings.json: ignored [], load_one_by_one false
#   launch 3 (same config): PlsA and PlsB both run with no setup commands at all (the folder persisted, nothing ignored)
# ---------------------------------------------------------------------------
pls_ok() { echo "ok   $1"; }
pls_bad() { echo "FAIL $1"; fail=1; }
if ! command -v cc >/dev/null 2>&1 || ! command -v python3 >/dev/null 2>&1 || [ "$IS_WINDOWS" = 1 ]; then
  echo "skip PLS_all (needs a C compiler and python3; not run on Windows)"
else
  case "$(uname -s)" in Darwin) PLS_EXT=dylib; PLS_LINK="-dynamiclib" ;; *) PLS_EXT=so; PLS_LINK="-shared" ;; esac
  PLS_DIR="$TMPW/pls"; PLS_PLUG="$PLS_DIR/plugins"; PLS_CFG="$PLS_DIR/config"
  mkdir -p "$PLS_PLUG" "$PLS_CFG"
  pls_src() {  # pls_src <letter> <Command> <PluginName>
    cat > "$PLS_DIR/pls_$1.c" <<EOC
#include "dino8_plugin.h"
static const Dino8PluginApi* g_api;
static int Cmd(int argc, const char** argv, Dino8CommandContext* ctx) { (void)argc; (void)argv; (void)ctx; g_api->print("PLS_$1_RAN"); return 0; }
DINO8_PLUGIN_EXPORT int dino8_plugin_init(const Dino8PluginApi* api) {
  g_api = api;
  if (api->api_version != DINO8_PLUGIN_API_VERSION) return 1;
  api->register_command("$2", "PLS test command", Cmd, 0);
  return 0;
}
DINO8_PLUGIN_EXPORT const char* dino8_plugin_name(void) { return "$3"; }
EOC
  }
  pls_src A PlsA PlsAlpha
  pls_src B PlsB PlsBeta
  if cc $PLS_LINK -fPIC -I "$HERE/../include" -o "$PLS_PLUG/pls_alpha.$PLS_EXT" "$PLS_DIR/pls_A.c" 2>"$PLS_DIR/cc_a.log" && \
     cc $PLS_LINK -fPIC -I "$HERE/../include" -o "$PLS_PLUG/pls_beta.$PLS_EXT" "$PLS_DIR/pls_B.c" 2>"$PLS_DIR/cc_b.log"; then
    pls_ok "PLS_build: two throwaway plug-in libraries compiled against include/dino8_plugin.h"
  else
    pls_bad "PLS_build: could not compile the test plug-ins"; cat "$PLS_DIR/cc_a.log" "$PLS_DIR/cc_b.log" 2>/dev/null | head -5
  fi
  A_EXT="pls_alpha.$PLS_EXT"; B_EXT="pls_beta.$PLS_EXT"

  cat > "$PLS_DIR/l1.txt" <<EOS
@wait 3
GrasshopperIgnorePlugin $B_EXT
PLSBEGIN_dup1
GrasshopperFolders add $PLS_PLUG
PLSBEGIN_dup2
GrasshopperFolders add $PLS_PLUG
PLSBEGIN_ignore_dup
GrasshopperIgnorePlugin $B_EXT
GrasshopperLoadOneByOne
PLSBEGIN_run
PlsA
PlsB
PLSBEGIN_disable
PlugInDisable PlsAlpha
PlsA
GrasshopperPluginList
PLSBEGIN_end
EOS
  cat > "$PLS_DIR/l2.txt" <<EOS
@wait 3
PLSBEGIN_state
GrasshopperFolders
GrasshopperLoadOneByOne
GrasshopperIgnorePlugin
PlsA
PlsB
PLSBEGIN_enable
PlugInEnable pls_alpha
PlsA
PLSBEGIN_ignored1
GrasshopperIgnorePlugin
PLSBEGIN_unignore
GrasshopperIgnorePlugin remove pls_beta
PlsB
PLSBEGIN_ignored0
GrasshopperIgnorePlugin
PLSBEGIN_end
EOS
  cat > "$PLS_DIR/l3.txt" <<EOS
@wait 3
PLSBEGIN_fresh
PlsA
PlsB
GrasshopperFolders
GrasshopperLoadOneByOne
PLSBEGIN_end
EOS
  pls_run() {  # pls_run <script>
    if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
      XDG_CONFIG_HOME="$PLS_CFG" HOME="$PLS_CFG" timeout 600 "$BIN" --smoke 30 --script "$1" 2>&1
    else
      XDG_CONFIG_HOME="$PLS_CFG" HOME="$PLS_CFG" timeout 600 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$1" 2>&1
    fi
  }
  PLS_L1="$(pls_run "$PLS_DIR/l1.txt")" || { echo "$PLS_L1" | tail -20; pls_bad "PLS_run1: app exited non-zero"; PLS_L1=""; }
  PLS_SET1="$(cat "$PLS_CFG/dino8/settings.json" 2>/dev/null || true)"
  PLS_L2="$(pls_run "$PLS_DIR/l2.txt")" || { echo "$PLS_L2" | tail -20; pls_bad "PLS_run2: app exited non-zero"; PLS_L2=""; }
  PLS_SET2="$(cat "$PLS_CFG/dino8/settings.json" 2>/dev/null || true)"
  PLS_L3="$(pls_run "$PLS_DIR/l3.txt")" || { echo "$PLS_L3" | tail -20; pls_bad "PLS_run3: app exited non-zero"; PLS_L3=""; }

  pls_block() {  # pls_block <log-var-name> <block>: lines between PLSBEGIN_<block> and the next PLSBEGIN_
    awk -v name="$2" '$0 ~ ("Unknown command: PLSBEGIN_" name "$") { on = 1; next } on && /Unknown command: PLSBEGIN_/ { exit } on { print }' <<<"${!1}"
  }
  pls_has() { if grep -qF -- "$3" <<<"$(pls_block "$1" "$2")"; then pls_ok "$4"; else pls_bad "$4"; echo "     expected: $3"; fi; }
  pls_hasnot() { if grep -qF -- "$3" <<<"$(pls_block "$1" "$2")"; then pls_bad "$4"; else pls_ok "$4"; fi; }
  pls_json() { if grep -qF -- "$2" <<<"${!1}"; then pls_ok "$3"; else pls_bad "$3"; echo "     expected in settings.json: $2"; { grep -F '"plugins"' <<<"${!1}" || true; } | head -2 | sed 's/^/     actual:   /'; fi; }

  pls_has PLS_L1 dup1 "GrasshopperFolders: added $PLS_PLUG" "PLS_dup: the first 'GrasshopperFolders add' adds the folder"
  pls_has PLS_L1 dup2 "is already in the list" "PLS_dup: the same folder added again is refused as a duplicate"
  pls_has PLS_L1 ignore_dup "is already ignored" "PLS_dup: ignoring the same library twice is refused as a duplicate"
  pls_has PLS_L1 run "PLS_A_RAN" "PLS_run: the plug-in in the added folder loaded and its command ran"
  pls_has PLS_L1 run "Unknown command: PlsB" "PLS_run: the ignored plug-in's command does not exist"
  pls_has PLS_L1 disable "PlugInDisable: PlsAlpha disabled" "PLS_disable: PlugInDisable PlsAlpha reports it"
  pls_hasnot PLS_L1 disable "PLS_A_RAN" "PLS_disable: a disabled plug-in's command no longer runs"
  pls_has PLS_L1 disable "its plug-in is disabled" "PLS_disable: ... and says why"
  pls_has PLS_L1 disable "PlsAlpha  - 1 command(s), 0 node(s) [disabled]" "PLS_disable: GrasshopperPluginList marks it [disabled]"
  pls_json PLS_SET1 "\"folders\": [\"$PLS_PLUG\"]" "PLS_persist: settings.json carries the plug-in folder, once"
  pls_json PLS_SET1 "\"ignored\": [\"$B_EXT\", \"$A_EXT\"]" "PLS_persist: settings.json carries the ignored list (the ignored library, then the disabled one)"
  pls_json PLS_SET1 "\"load_one_by_one\": true" "PLS_persist: settings.json carries load_one_by_one = true"

  pls_has PLS_L2 state "GrasshopperFolders: 1 extra folder(s)" "PLS_restart: a fresh process still lists the folder (1)"
  pls_has PLS_L2 state "GrasshopperLoadOneByOne: off" "PLS_restart: load-one-by-one was restored as on (the toggle now reads off)"
  pls_has PLS_L2 state "GrasshopperIgnorePlugin: 2 plug-in(s) ignored" "PLS_restart: both ignored/disabled libraries are still ignored (2)"
  pls_has PLS_L2 state "Unknown command: PlsA" "PLS_restart: the disabled plug-in did not load"
  pls_has PLS_L2 state "Unknown command: PlsB" "PLS_restart: the ignored plug-in did not load"
  pls_has PLS_L2 enable "PlugInEnable: loaded $A_EXT" "PLS_enable: PlugInEnable pls_alpha (library name; a skipped plug-in has no display name yet) loads it"
  pls_has PLS_L2 enable "PLS_A_RAN" "PLS_enable: ... and its command runs"
  pls_has PLS_L2 ignored1 "GrasshopperIgnorePlugin: 1 plug-in(s) ignored" "PLS_enable: one ignored library is left"
  pls_has PLS_L2 unignore "GrasshopperIgnorePlugin: loaded $B_EXT" "PLS_unignore: 'GrasshopperIgnorePlugin remove pls_beta' loads it"
  pls_has PLS_L2 unignore "PLS_B_RAN" "PLS_unignore: ... and its command runs"
  pls_has PLS_L2 ignored0 "GrasshopperIgnorePlugin: 0 plug-in(s) ignored" "PLS_unignore: nothing is ignored any more"
  pls_json PLS_SET2 "\"ignored\": []" "PLS_persist: the emptied ignored list is saved as empty"
  pls_json PLS_SET2 "\"load_one_by_one\": false" "PLS_persist: the toggled-off flag is saved"

  pls_has PLS_L3 fresh "PLS_A_RAN" "PLS_fresh: after enabling, a brand-new process loads PlsAlpha with no setup commands"
  pls_has PLS_L3 fresh "PLS_B_RAN" "PLS_fresh: ... and PlsBeta (nothing is ignored, the folder persisted)"
  pls_has PLS_L3 fresh "GrasshopperLoadOneByOne: on" "PLS_fresh: load-one-by-one restored as off (toggle now reads on)"
fi
