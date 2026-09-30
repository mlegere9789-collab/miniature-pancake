# Accessibility in Dino 8

This document is an honest account of what Dino 8's UI does and does not
support for users with disabilities, given that the UI is built on
[Dear ImGui](https://github.com/ocornut/imgui). It covers four areas: a
high-contrast theme (shipped), full keyboard operability (audited and
fixed where it was broken), screen-reader support for the command line, the
main menu bar, the running command's options, the Layers/Properties panels,
each viewport's title/view-menu button, and the persisted Activity Log of
finalized edits (a real, still-narrow AT-SPI2 bridge, shipped on Linux - see
section 3), and screen-reader support for the rest of the UI (still a hard
platform limitation of ImGui itself for the reasons section 3 explains - not
shipped, and not something a few labels can fix).

## 1. High Contrast theme - shipped

Options > General > Theme now has three entries: **Dark**, **Light**, and
**High Contrast**. High Contrast is a dedicated palette, not a filter over
Dark or Light:

- Pure black window background (`#000000`) with pure white text (`#FFFFFF`)
  - contrast ratio ~21:1, far above the WCAG AA minimum of 4.5:1 for body
    text and 3:1 for large text / UI components.
- Disabled ("muted") text is a light grey (`#B8B8B8`) on black -
  ~9.6:1, still clearing 4.5:1 comfortably rather than the common mistake
  of dimming disabled text into invisibility.
- A fixed, saturated yellow accent (`#FFD200`) for selection, focus and
  active states - ~17:1 against black. The accent picker in Options is
  disabled while High Contrast is active so a user can't accidentally pick
  a low-contrast accent colour; switching back to Dark or Light restores
  whatever accent was chosen before.
- Every frame, popup and tab gets a full-opacity 1-2px white border
  (`FrameBorderSize`/`TabBorderSize`/`WindowBorderSize` are 0-1px in
  Dark/Light but forced on in High Contrast). This matters because Dark
  and Light mostly distinguish hovered/active/selected state by *tinting*
  a translucent fill - fine when you can perceive that hue shift, not
  something to rely on alone. High Contrast additionally draws a visible
  outline change, so state is legible from shape, not colour alone.

This is a genuine attempt at WCAG-AA-adjacent contrast for the pieces of
the UI Dino 8 controls directly (ImGui's own style colours). It has not
been run through a certified contrast-checking tool against every possible
combination (e.g. a custom plugin panel could still paint low-contrast
text with `ImGui::TextColored`), so treat "WCAG-AA-ish" as accurate:
strong effort, not a certification.

Note: the 3D viewport's own rendering (grid, wireframe colours, layer
colours) is unrelated to this palette and is unaffected by High Contrast -
this theme only reaches ImGui chrome (menus, panels, toolbars, dialogs).

Implementation: `src/ui/Theme.h`/`.cpp` (`ThemeMode` enum, `ApplyDinoTheme`),
wired into `Application::theme_mode` (`src/app/Application.h`), the Options
window (`src/ui/Panels.cpp`), and persisted in `Settings.cpp` under the
`theme_mode` key (the older `light_theme` boolean is still written for
backward compatibility with configs saved by earlier Dino 8 builds).

## 2. Full keyboard-only operability - audited and fixed

### What was already keyboard-accessible

- `ImGuiConfigFlags_NavEnableKeyboard` was already set (`src/main.cpp`), so
  ImGui's built-in keyboard navigation (Tab/Shift+Tab between widgets,
  arrow keys inside a widget group, Enter/Space to activate, Esc to close
  popups) works throughout the standard ImGui windows, panels, dialogs and
  the main menu bar out of the box - this is "largely free" in ImGui once
  the flag is on, and it already was.
- The command line (`Application::DrawCommandLine`) is keyboard-first: any
  typing anywhere in the app routes into it (`focus_command_line_`), Up/Down
  cycle command history or the autocomplete popup, and every command in the
  Standard toolbar, every other toolbar tab, and the sidebar can be run by
  typing its name, with no mouse involved - this mirrors Rhino's own
  command-line-driven workflow and means no toolbar action is *unreachable*
  from the keyboard even before the fix below.
- Menu items already show their keyboard shortcut and are fully Tab/Enter
  operable (`src/ui/MenuBar.cpp`).

### The bug found and fixed: toolbar/sidebar buttons were mouse-only

The Standard toolbar, the nine tool tabs (Curve Tools, Surface Tools, Solid
Tools, ...), the toolbar tab strip itself, the left sidebar, the
notifications bell, and each viewport's title/view-menu button are all
built on `ImGui::InvisibleButton()` (`src/ui/Toolbars.cpp`,
`src/app/Application.cpp`, `src/viewport/Viewport.cpp`) rather than
`ImGui::Button()`. **`InvisibleButton()` opts a widget out of keyboard and
gamepad navigation by default** - it needs `ImGuiButtonFlags_EnableNav`
explicitly, unlike `ImGui::Button()` which is nav-enabled automatically.
None of the call sites passed that flag, so:

- Tab could not move focus onto any toolbar or sidebar tool button, the
  toolbar tab strip, the notification bell, or a viewport's title/view
  button - these were skipped entirely by keyboard/gamepad navigation even
  though the global `NavEnableKeyboard` flag was on.
- The buttons were still reachable *functionally* (the same commands run
  from a menu item or by typing the command name), but a keyboard-only
  user tabbing through the UI to see and operate the toolbar directly had
  no way to land on or activate one of these buttons with the keyboard.

**Fix**: added `ImGuiButtonFlags_EnableNav` to every `InvisibleButton()`
call that represents a real, standalone clickable control:

- `IconButton()` in `src/ui/Toolbars.cpp` - every toolbar and sidebar tool
  button (used by the Standard toolbar, all nine tool tabs, and the left
  sidebar).
- The toolbar tab strip's tab buttons (Standard / Curve Tools / ... / View)
  in `DrawToolbars()`, `src/ui/Toolbars.cpp`.
- The notifications bell in the status bar, `Application::DrawStatusBar`
  (called from `src/app/Application.cpp`).
- Each viewport's title/view-menu button, `src/viewport/Viewport.cpp`.

These are now real Tab stops: Tab/Shift+Tab cycles onto them, ImGui draws
its standard nav-focus outline (`RenderNavCursor`, automatic once
`EnableNav` is set), and Enter/Space activates them exactly like a mouse
click.

Left as mouse/drag-only, deliberately, because a keyboard equivalent isn't
a meaningful substitute for the interaction itself: the gumball's drag
handles (`src/ui/Gumball.cpp`), the Dino Flow node-editor canvas panning
(`src/flow/FlowEditor.cpp`), the colour-picker's hue/SV widgets (ImGui's
own `ColorEdit3`/`ColorPicker3`, `imgui_widgets.cpp`), and free 3D-viewport
orbit/pan/zoom (`src/viewport/Viewport.cpp`). All of the underlying
operations they perform (move/rotate/scale an object, set a colour by
typed hex/RGB, get to Top/Front/Right/Perspective, zoom to a named view or
selection) have separate, fully keyboard-typeable command equivalents;
free-form continuous 3D navigation by feel does not have a discrete
keyboard substitute in Dino 8, the same as in Rhino itself and virtually
every other 3D CAD/DCC application.

### Screen-reader-adjacent fix: nav-focus tooltips

A related gap, fixed alongside the above: `IconButton()`'s descriptive
tooltip (name, status badge, description, click/right-click legend) was
only ever shown on mouse hover (`RichTooltip`, gated on
`ImGui::IsItemHovered`). A keyboard user tabbing onto an icon-only button -
every button on the left sidebar, and any toolbar with captions turned
off - had no way to find out what it did without also using a mouse.
`IconButton()` now shows the same tooltip when the button has nav focus
(`ImGui::IsItemFocused()`), positioned under the button rather than at the
mouse cursor. This does not require or imply a screen reader (see below);
it just means a sighted keyboard-only user gets the same identifying text
a mouse user already got from hovering.

### What "full keyboard-only workflow coverage" means here, honestly

Every command in Dino 8's ~1000+ command catalog was already reachable by
typing its name at the command line with no mouse at all - that has not
changed. What changed is that the *toolbar/sidebar UI surface itself* (not
just the commands behind it) is now keyboard-navigable, which matters for
a user who wants to explore or operate the UI as presented rather than
knowing command names in advance. This was checked by reading every
`ImGui::InvisibleButton` call site in the UI and app source (there are 6
total; 4 were real standalone buttons and got the fix, 2 - the color
picker's hue/SV drag area and the Flow canvas background - are inherently
drag/paint surfaces where a "Tab stop" would not be meaningful) and by
confirming `ImGuiConfigFlags_NavEnableKeyboard` is set. It was not checked
by running an automated accessibility scanner or manually tabbing through
every one of Dino 8's ~40 panels/dialogs frame by frame; a residual keyboard
trap in a less-visited dialog is possible and would need to be reported
and fixed the same way as the toolbar bug above.

## 3. Screen-reader support

**Dear ImGui has no built-in accessibility-tree integration**, and the
vendored docking branch this project builds against does not add one
either - there is no `IMGUI_ENABLE_ACCESSIBILITY`-style macro, no
platform-backend accessibility hooks anywhere in `third_party/imgui`, and
no `AtkObject`/`NSAccessibility`/`IAccessible`/AT-SPI callback of any kind
in the GLFW backend it uses. ImGui draws every widget as textured
triangles into a single OpenGL/Vulkan/DirectX framebuffer; there is no
retained widget tree and no OS-level control handle for a screen reader to
attach to, for *any* widget. That much of the original assessment below
still holds for the UI as a whole:

- **Windows**: no UI Automation (UIA) or MSAA integration. A screen reader
  like NVDA or Narrator sees an opaque window with no children.
- **macOS**: no `NSAccessibility` integration. VoiceOver sees the same
  opaque window.
- **Linux**: no accessibility-tree bridge for the 3D viewport, panels, or
  dialogs. Orca sees the same opaque window for those.

Wiring the *entire* UI up this way is still a major, platform-specific
undertaking with no shortcut: it means maintaining a shadow accessibility
tree that mirrors every ImGui window/widget per frame and pushing it
through each platform's native accessibility API - exactly the scope the
ImGui maintainers have discussed for years without landing project-wide.
That has not changed and is not what shipped here.

### What has shipped: a real AT-SPI2 bridge for the command line, the menu bar, the Layers/Properties panels, the viewports, and the Activity Log (Linux)

The one place in Dino 8 blind command-line-driven use is already the
primary interaction model - the command line itself
(`Application::DrawCommandLine`, backed by `command_input_` and
`CommandEngine::History()`, see section 2) - has a real accessibility
bridge on Linux: `src/platform/AccessibilityLinux.cpp` implements the
actual AT-SPI2 D-Bus protocol (`org.a11y.atspi.Accessible`, `.Application`,
`.Text`) by hand, on the app's own D-Bus connection, and registers with the
real `at-spi2-registryd` via `org.a11y.atspi.Socket.Embed` - the same
mechanism GNOME's own `atk-bridge-2.0` uses for every GTK app. This is not
a toy or a simulation: it is the real protocol, verified end-to-end against
the real registry daemon and the real `pyatspi` client library (see
"Verifying it yourself" below). The same bridge also publishes the main
menu bar, the Layers/Properties panels' content, each viewport's title/
view-menu button state, and the Activity Log, described below.

**Command line**: exactly one accessible object, named "Command Line"
(`ATSPI_ROLE_LOG` - "a text widget or container holding log content"),
under the application root. Its `Text.GetText()` returns the full
command-history log (`CommandEngine::History()`, one line per entry),
followed by the current prompt and whatever is live in the input buffer -
the same three pieces of information the on-screen `##CommandLine` window
shows, refreshed every frame (`platform::UpdateAccessibility`, called from
`main.cpp`'s frame loop). A screen reader (or `pyatspi`/`dbus-send`) can
read this value at any time and see it change the moment a command runs,
without needing to see the screen at all.

**Menu bar**: a "Menu Bar" accessible (`ATSPI_ROLE_MENU_BAR`) with one
"Menu" child (`ATSPI_ROLE_MENU`) per top-level menu (File, Edit, View, ...)
- always present, since the menu bar itself is always on screen - and, for
whichever single menu the user currently has open, its actual items
(`ATSPI_ROLE_MENU_ITEM`, name including the shortcut, e.g. `"Save
(Ctrl+S)"`) and any open submenu nested the same way. This mirror is not a
hand-duplicated copy of the menu catalog: `src/ui/MenuBar.cpp` builds it
with a `platform::MenuTreeBuilder` (see `AccessibilityTree.h`) right
alongside the real `ImGui::BeginMenu`/`MenuItem` calls that draw the menu,
via thin wrapper functions (`BeginMenuA`/`EndMenuA`/`MenuItemA`/`Item`), so
the accessible tree is built from exactly what ImGui actually drew that
frame and can never drift out of sync with the real menu - a submenu that
isn't open just never gets its items recorded, honestly matching what's on
screen (ImGui itself never runs a closed submenu's drawing code either).

**Command options**: a "Command Options" accessible (`ATSPI_ROLE_LIST`),
present at all times (empty when no command is running or the running
command offers none), with one `ATSPI_ROLE_LIST_ITEM` per option the
currently running command offers - the same chips `DrawCommandLine` draws
next to the prompt (e.g. running `Circle` shows "Diameter", "3Point",
"Vertical"; a numeric option in progress shows as `"Radius=5"`). Each
item's `Description` gives the exact interaction guidance the chip's own
mouse-hover tooltip gives a sighted user: click to toggle, click to cycle
through a fixed list of choices (every choice is named), click then type a
new value, or just type the option's name - so a screen-reader user gets
the same "how do I change this" answer without needing to see or hover the
chip. Built from `CommandEngine::CurrentOptions()`
(`ui::CommandOptionsAccessibleTree`, `src/ui/Panels.cpp`), independent of
whether the command-line window itself is visible (`Options > ... command
prompt` can be hidden - see `Application::CommandLineHeight`) - a
screen-reader user can always ask what the running command offers, the
same way Layers/Properties don't depend on their panel windows being open.

**Layers and Properties panels**: a "Layers" accessible (`ATSPI_ROLE_LIST`)
with one `ATSPI_ROLE_LIST_ITEM` per layer (name, current/visible/locked
state, object count - the same facts the on-screen Layers table's
Name/Cur/On/Lock/Objects columns show), and a "Properties" accessible
(also `ATSPI_ROLE_LIST`) with one list item per `"Label: value"` fact about
the current selection (or the document/active viewport, when nothing is
selected) - the same facts `DrawPropertiesPanel` shows. A Properties row
that mirrors a real editable widget in `DrawPropertiesPanel`'s "Object"
section (Name, Layer, Color source, Locked, Linetype, Material - anything
the user can actually type into, check or pick from a combo, as opposed to
a plain fact like Type or Group) carries a `Description` saying so and
naming the field, so a screen-reader user can tell a value editor apart
from read-only text (`platform::PropertyEntry::editable`,
`ui::PropertiesPanelAccessibleTree`) - though, like the rest of this
bridge, it is still read-only from AT-SPI's own side: making the actual
change still means driving the real widget or the equivalent command by
name (see "Known gaps" below). Unlike the menu bar, these are built
directly from `Document`/`Application` state
(`ui::LayersPanelAccessibleTree`/`PropertiesPanelAccessibleTree`,
`src/ui/Panels.cpp`), independent of whether either panel window is
actually open on screen right now, so a screen-reader user always has
access to this information regardless of which panels happen to be
toggled visible.

**Viewports**: a "Viewports" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per viewport (Top, Front, Right, Perspective, ... -
whichever the current layout holds), naming which one currently has input
focus, whether it's maximized (the others are hidden on screen while any one
is, though this list still names all of them - see "Known gaps"), and its
current display mode (Wireframe, Shaded, Rendered, ...) - the same facts the
title pill/view-menu button drawn over each viewport (`Viewport::DrawUI`'s
title-overlay block) and the display-mode label in its corner already show a
sighted user. Built from `Application::Viewports()`
(`ui::ViewportsAccessibleTree`, `src/ui/Panels.cpp`), independent of which
viewport window happens to be visible on screen right now, the same way
Layers/Properties don't depend on their own panel windows being open. This
does not expose the viewport's actual 3D content (see "Why these regions and
not the rest of the UI" below) - only the state of its title/view-menu
control, the same load-bearing-but-narrow scope as the other regions here.

**Activity Log**: an "Activity Log" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per recorded edit, naming its timestamp, action label
and object-count summary (e.g. `"2024-01-01 12:00:00 Line: +1 -0 ~0
object(s) [ids 12]"`) - the same three facts the on-screen Activity Log
panel's Time/Action/Detail columns show for that row. Built from
`Document::ActivityLog()` (`ui::ActivityLogAccessibleTree`,
`src/ui/Panels.cpp`), independent of whether the Activity Log panel window is
actually open on screen right now, the same way Layers/Properties/Viewports
don't depend on their own panel windows being open. This is a genuinely
separate record from the "Command Line" accessible's text: the command line
mirrors `CommandEngine::History()` (every line printed, including notices
and errors that never produce a persisted edit), while the Activity Log
mirrors `Document::ActivityLog()` - the persisted, structured record of only
the edits that actually finalized (`Document::RecordActivityLogEntry`,
labels/ids/counts only, no geometry - see that function's own comment for
why), which also survives past the current session in a sidecar file next
to the document (`Document::LoadActivityLog`/`FlushActivityLogToDisk`). A
screen-reader user can review what happened to the document - not just what
was typed - without needing to see the on-screen table at all.

**Why these regions and not the rest of the UI**: the command line is the
one region where "expose the text" is both sufficient (there is no
meaningful spatial layout to convey - it *is* a stream of text) and
complete on its own (every command in the ~1000+ catalog is already
reachable by typing into it, per section 2). The menu bar, the
Layers/Properties panels, the viewports and the Activity Log extend this to
the next-most load-bearing UI surfaces - discovering what commands exist by
name, inspecting/editing layer and object state, knowing where you're
looking, and reviewing what actually happened to the document - without
requiring the full shadow-tree-for-every-widget effort described above.
Mirroring the 3D viewport and the ~39 remaining panels/dialogs the same way
would still need that effort; this does not extrapolate to "screen reader
support" for those in the way a browser or native-toolkit app would provide
it, and this document does not claim otherwise.

**Build-time and platform scope**: `AccessibilityLinux.cpp` is compiled
only when `libatspi2.0-dev` and `libglib2.0-dev` (`atspi-2`/`gio-2.0` via
`pkg-config`) are found at CMake configure time (see `CMakeLists.txt`) -
this is a soft dependency, not a hard build requirement, so a build
environment without them (most CI containers, most non-desktop Linux
boxes) still produces a working Dino8, just without an AT-SPI-visible
command line. `platform/Accessibility.h`'s functions become no-ops in that
case, on Windows/macOS (no bridge implemented there - AT-SPI2 is
Linux-specific; UIA/NSAccessibility bridges for this same one region would
be the natural next step but are not implemented), and if the AT-SPI2 bus
itself (`org.a11y.Bus`/`at-spi2-registryd`) simply isn't running at
process start (accessibility turned off, headless server with no a11y
daemon) - every one of those is a silent, logged no-op, never a crash or a
hang.

**Known gaps in the bridge itself**, for whoever extends it next:

- `GetState` always returns an empty state set rather than real
  focus/visible/enabled bits - a deliberate choice (an empty set is
  honestly "unknown", a wrong bitfield could read as "hidden" to some
  clients) but a fuller implementation would report real state. This means
  a screen reader can enumerate menu items and layer/property rows but
  cannot yet ask AT-SPI which one currently has keyboard focus.
- No `Component` interface (no bounding-box/extents), so a client that
  expects to *locate* a region on screen (rather than just read its
  text/children) has nothing to query.
- The command line region is read-only from AT-SPI's side: `Text.SetCaretOffset`
  is implemented but always reports failure, since there is no caret to
  move independently of typing.
- The live input and the history log are merged into one `Text` value
  rather than exposed as two separate accessible objects - chosen to
  match what the on-screen widget itself shows as a single unit, but a
  screen-reader UX designer might reasonably want them distinguishable.
- The root's `Parent` property is a null reference rather than the
  registry's real desktop reference - harmless for the forward traversal
  (`Desktop -> Application -> ...`) this bridge is verified against, but
  would matter for a client that walks upward from a reference it already
  holds.
- The menu bar mirror only has items for whichever *one* menu the user
  currently has open (matching ImGui's own single-open-popup behavior); it
  does not attempt to reconstruct the full ~1000-command menu catalog as a
  static, always-populated tree. A client that wants the whole catalog
  without opening every menu should use the command line's autocomplete
  instead (see section 2) - functionally complete, just not exposed as a
  menu tree.
- The Layers/Properties accessibles have no `Action` interface (no
  AT-SPI-driven "toggle this layer's visibility" or "rename this object");
  they are read-only views. Making a change still requires driving the
  real widget (mouse) or the equivalent command by name (keyboard/screen
  reader via the command line). This now includes Properties rows flagged
  `editable` (see above): the flag tells a screen-reader user a value
  editor exists, not a way to drive it over AT-SPI.
- Command Options has the same gap: no `Action` interface, so a client
  cannot click a chip over AT-SPI itself - only read its name/value and the
  plain-text guidance for the equivalent mouse click or typed option name.
- Viewports has the same read-only gap (no `Action` interface - switching
  the active viewport, changing its display mode, or maximizing/restoring
  it still means driving the real title/view-menu button or the equivalent
  command by name), and it lists every viewport even while one is
  maximized and the others are hidden on screen - matching real,
  queryable `Application::Viewports()` state rather than only what's
  currently drawn, unlike the menu bar's "only what's open" rule above; a
  screen-reader user reading "Top" while Perspective is maximized should
  understand it as "exists but hidden", not "visible right now".
- Activity Log has the same read-only gap (no `Action` interface - there is
  nothing to click here anyway, since a row is a historical fact rather than
  a live control, but there is also no AT-SPI-driven equivalent of the
  on-screen panel's Export CSV button or its filter/date-range fields; a
  screen-reader user gets the full unfiltered log as plain text and would
  filter it themselves, or use `ActivityExport`/the command line, the same
  workaround Command Options and Layers/Properties already need for their
  own missing `Action` interface). It also has no cap: a very long editing
  session's full Activity Log is walked and re-published every frame, same
  as every other list here - fine at the sizes this was verified with, but
  worth knowing about for a document with an unusually large edit history.

**Internal design, independent of AT-SPI itself**: the accessible tree's
*shape and text* are built by a small, pure, platform-independent module,
`src/platform/AccessibilityTree.h`/`.cpp` (`BuildAccessibleTree`,
`BuildCommandLineText`, `MenuTreeBuilder`, `BuildLayersPanelNode`,
`BuildPropertiesPanelNode`, `BuildCommandOptionsNode`,
`BuildViewportsPanelNode`, `BuildActivityLogNode`), with its own unit test
(`tests/test_accessibility_tree.cpp`, registered as the
`dino8_accessibility_tree` CTest target) that needs no display, no D-Bus, and
no AT-SPI2 build at all - it runs on every platform and every CI job. The
menu bar's mirror is recorded live, right alongside the real ImGui
menu-drawing calls in `src/ui/MenuBar.cpp` (`MenuTreeBuilder`'s
`OpenMenu`/`CloseMenu`/`LeafMenu`/`Item`, driven by that file's
`BeginMenuA`/`EndMenuA`/`MenuItemA`/`Item` wrappers), so it can never drift
from what was actually drawn. The Layers, Properties, Command Options,
Viewports and Activity Log mirrors (`src/ui/Panels.cpp`'s
`LayersPanelAccessibleTree`/`PropertiesPanelAccessibleTree`/
`CommandOptionsAccessibleTree`/`ViewportsAccessibleTree`/
`ActivityLogAccessibleTree`) are built straight from
`Document`/`Application`/`CommandEngine` state, independent of
`DrawLayersPanel`/`DrawPropertiesPanel`/`DrawCommandLine`/`Viewport::DrawUI`/
`DrawActivityLogPanel`. `AccessibilityLinux.cpp` is a thin transport on top
of all of this: every frame it receives the whole tree wholesale
(`platform::PlatformSetAccessibleTree`) and answers AT-SPI's
`Text.GetText`/`Accessible.GetChildren`/etc. by walking it - one
`g_dbus_connection_register_subtree()` handler covers the whole tree since
which nodes even exist (which submenu is open, how many layers exist)
changes every frame. This split means the one part that's genuinely hard
to unit-test (a live D-Bus service) is also the one part with the least
logic in it.

### Verifying it yourself

`tests/smoke_accessibility.py` is a real, automated, end-to-end integration
test: it spawns the actual built `Dino8` binary under `--smoke`, starts a
real `dbus-daemon` and the real `at-spi2-registryd`, and uses the real
`pyatspi` client library to walk the AT-SPI2 desktop and find Dino8's
"Command Line", "Menu Bar" (with its "File" child), "Layers", "Properties",
"Command Options", "Viewports" (with its default "Perspective" row
reporting itself active) and "Activity Log" accessibles - the same objects a
screen reader would find - then asserts the command line's and the
Properties list's content each change after a real command (`Line 0,0,0
10,10,0`) runs, that Command Options goes empty -> lists Circle's option
chips -> empty again around a real running `Circle` command, and that the
Activity Log gains a new entry naming `Line` right after that same command
finishes. It does not fake, mock, or stub any part of the AT-SPI2 stack.

This was run successfully, including the new Activity Log checks, in the
environment this addition was built and verified in, after installing:
`libatspi2.0-dev`, `libglib2.0-dev`, `at-spi2-core` (provides
`at-spi2-registryd`), `dbus-x11` (provides `dbus-daemon`), and
`python3-pyatspi` (Ubuntu 24.04/noble package names) - all nine checks
above passed against the real registry daemon, including "Command Options
lists Circle's option chips while it is running (['Diameter', '3Point',
'Vertical'])" and "Activity Log gains a new entry naming the finalized
command after it runs". One environment-specific wrinkle worth knowing about, not
specific to this project: Debian/Ubuntu's `python3-pyatspi`/`python3-gi`
ship a `gi._gi` extension compiled for one specific CPython ABI (on the box
this was verified on, that was `python3.12`, even though the default
`python3` on `PATH` resolved to a different, incompatible CPython build) -
if `import pyatspi` fails in whatever interpreter you run the script with,
find the interpreter whose compiled extension actually matches (the module
docstring in `tests/smoke_accessibility.py` explains how) and re-run with
that one explicitly, e.g. `python3.12 tests/smoke_accessibility.py
build/Dino8`. This is an environment/packaging detail, not a defect in the
bridge or the test.

`dino8_accessibility_tree` (the internal-tree unit test above) has no such
requirement and runs as part of the normal CTest suite everywhere.

## Summary for RHINO8_KILLER_AUDIT.md

| Area | Status |
|---|---|
| High-contrast theme | Shipped: Options > General > Theme > High Contrast |
| Keyboard-only operability | Audited; one real bug found and fixed (toolbar/sidebar/tab-strip/bell/viewport-title buttons were `InvisibleButton` without `EnableNav`, so Tab skipped them); nav-focus tooltips added for icon-only buttons; free 3D viewport orbit and a few inherently-drag widgets remain mouse-only by design, same as in Rhino |
| Screen-reader support (command line, menu bar, command options, Layers/Properties panels, viewports, Activity Log) | Shipped on Linux: a real AT-SPI2 bridge (`src/platform/AccessibilityLinux.cpp`) exposes the command line's live text and full history log, the main menu bar (mirroring exactly what's currently open, built live alongside `ui/MenuBar.cpp`'s own drawing calls), the running command's options (with per-option guidance on how to change it), the Layers/Properties panels' current content (Properties rows note which are real value editors), every viewport's title/view-menu button state (name, active/maximized, current display mode), and the persisted Activity Log of finalized edits (timestamp/action/summary per entry, distinct from the command line's own raw text log) as queryable, updating accessible objects, verified end-to-end against the real registry daemon and `pyatspi` (`tests/smoke_accessibility.py`), including the new Activity Log checks. Windows/macOS not implemented. Built only when `atspi-2`/`gio-2.0` are available; a silent no-op otherwise |
| Screen-reader support (rest of the UI) | Not implemented - hard ImGui platform limitation (no accessibility-tree bridge for the 3D viewport's own rendered content or the ~39 remaining panels/dialogs on any OS). Descriptive text/tooltips exist everywhere as a prerequisite, but that is not screen-reader support |
