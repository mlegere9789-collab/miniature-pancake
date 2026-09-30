# Accessibility in Dino 8

This document is an honest account of what Dino 8's UI does and does not
support for users with disabilities, given that the UI is built on
[Dear ImGui](https://github.com/ocornut/imgui). It covers four areas: a
high-contrast theme (shipped), full keyboard operability (audited and
fixed where it was broken), screen-reader support for the command line, the
main menu bar, the running command's options, the Layers/Properties panels,
each viewport's title/view-menu button, the persisted Activity Log of
finalized edits, and the document's saved Named Views, Named CPlanes,
Linetypes, Materials, Clipping Planes, Layouts, Block Manager, Layer State
Manager, Document User Text, Lights, Annotation Styles and Notes (a real,
still-narrow AT-SPI2 bridge, shipped
on Linux - see section 3), and screen-reader support
for the rest of the UI (still a hard platform limitation of ImGui itself for
the reasons section 3 explains - not shipped, and not something a few
labels can fix).

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

### What has shipped: a real AT-SPI2 bridge for the command line, the menu bar, the Layers/Properties panels, the viewports, the Activity Log, Named Views, Named CPlanes, Linetypes, Materials, Clipping Planes, Layouts, Block Manager, Layer State Manager, Document User Text, Lights, Annotation Styles, Notes, Environments, Audit Results, Undo/Redo History, Hatch Patterns, and Plug-ins (Linux)

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
view-menu button state, the Activity Log, Named Views, Named CPlanes,
Linetypes, Materials, Clipping Planes, Layouts, Block Manager, Layer State
Manager, Document User Text, Lights, Annotation Styles, Notes, Environments,
Audit Results, Undo/Redo History, Hatch Patterns, and Plug-ins, described
below.

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

**Named Views**: a "Named Views" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per saved view, named after it - the same single fact
the on-screen Named Views panel's row shows per saved view (no camera detail
is shown there either - see `DrawNamedViewsPanel`). Built from
`Document::NamedViews()` (`ui::NamedViewsAccessibleTree`, `src/ui/Panels.cpp`),
independent of whether the Named Views panel window is actually open on
screen right now, the same way the other panel-backed regions above don't
depend on their own panel windows being open. A screen-reader user can save
and jump between named camera bookmarks entirely from the command line
(`NamedView Save <name>` / `Restore <name>` / `Delete <name>` / `List`, see
`cmd_view.cpp`'s `NamedViewCommand`) and confirm what got saved without
needing to see the panel at all.

**Named CPlanes**: a "Named CPlanes" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per saved construction plane, named after it - the
same single fact the on-screen Named CPlanes panel's row shows per saved
plane (the origin/x-axis/y-axis are a hover tooltip there, not part of the
row itself - see `DrawNamedCPlanesPanel`). Built from
`Document::NamedCPlanes()` (`ui::NamedCPlanesAccessibleTree`,
`src/ui/Panels.cpp`), independent of whether the Named CPlanes panel window
is actually open on screen right now, the same way the other panel-backed
regions above don't depend on their own panel windows being open. A
screen-reader user can save and restore named construction planes entirely
from the command line (`NamedCPlane Save <name>` / `Restore <name>` /
`Delete <name>` / `List`, see `cmd_viewtools.cpp`'s `NamedCPlaneCommand`) and
confirm what got saved without needing to see the panel at all.

**Linetypes**: a "Linetypes" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per linetype, named after it, each carrying a
`Description` giving its dash/gap pattern as plain text (e.g. `"Pattern: 5,
2"`, or `"Pattern: continuous"` for an empty pattern) - the same two facts
the on-screen Linetypes panel's Name/Pattern columns show per row (see
`DrawLinetypesPanel`). Unlike Named Views/Named CPlanes, this list is never
empty: every document starts with `Document::DefaultLinetypes()` (Continuous,
Dashed, Dots, DashDot, Center, Hidden, Border). Built from
`Document::Linetypes()` (`ui::LinetypesAccessibleTree`, `src/ui/Panels.cpp`),
independent of whether the Linetypes panel window is actually open on screen
right now, the same way the other panel-backed regions above don't depend on
their own panel windows being open. A screen-reader user can add a custom
linetype and confirm it saved entirely from the command line
(`SetCustomLinetype Name=<name> Pattern=<dash,gap,...>`, see
`cmd_annotate2.cpp`'s `SetCustomLinetypeCommand`) without needing to see the
panel at all.

**Materials**: a "Materials" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per material, named after it, each carrying a
`Description` giving its diffuse colour as plain `"Colour: R, G, B"` text
(e.g. `"Colour: 200, 200, 200"`) - the one fact the on-screen Materials
panel's colour swatch conveys per row (see `DrawMaterialsPanel`) that a
screen reader otherwise has no way to read; gloss, reflectivity,
transparency and texture stay in the expanded per-material editor only, not
part of the row itself, the same way Linetypes' row above leaves out
anything not shown in its own on-screen columns. Built from
`Document::Materials()` (`ui::MaterialsAccessibleTree`,
`src/ui/RenderPanels.cpp`), independent of whether the Materials panel
window is actually open on screen right now, the same way the other
panel-backed regions above don't depend on their own panel windows being
open. A screen-reader user can assign a material to the current selection
entirely from the command line (`RenderAssignMaterialToObjects`, see
`cmd_render.cpp`'s `AssignMaterialCommand`) and confirm which materials
exist and what colour each one is without needing to see the panel at all.

**Clipping Planes**: a "Clipping Planes" accessible (`ATSPI_ROLE_LIST`) with
one `ATSPI_ROLE_LIST_ITEM` per clipping plane, named after it with its on/off
state folded into the name (e.g. `"Section A, on"`) and a `Description`
giving its viewport scope - `"Clips every viewport"` or `"Clips N
viewport(s)"` - the same two facts the on-screen Clipping Planes panel's
row checkbox and hover tooltip give a sighted user (see
`DrawClippingPlanesPanel`; the plane's origin/normal stay tooltip-only there
too, so they stay out of this mirror the same way Named CPlanes' own
origin/axes do). Built from `Document::ClippingPlanes()`
(`ui::ClippingPlanesAccessibleTree`, `src/ui/Panels.cpp`), independent of
whether the Clipping Planes panel window is actually open on screen right
now, the same way the other panel-backed regions above don't depend on their
own panel windows being open. A screen-reader user can create and inspect
clipping planes entirely from the command line (`ClippingPlane`, see
`cmd_viewtools.cpp`) and confirm which ones exist, whether each is switched
on, and whether it clips every viewport or a chosen few, without needing to
see the panel at all.

**Layouts**: a "Layouts" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per layout, named after it with the currently active
one called out in its name (e.g. `"Layout 1, active"`) - `DrawLayoutsPanel`
distinguishes its active row only by selection highlight, so this mirror
spells it out in text the same way `BuildViewportsPanelNode` spells out
which viewport is active. Page size and per-detail state only appear in the
panel's own expanded editor for the active layout, so they stay out of this
row, matching what the panel's own top-level Selectable row shows (see
`DrawLayoutsPanel`); this also does not include the always-present "Model"
layout the panel lists first, since it isn't a `Document::Layouts()` entry -
see `Application::ActiveLayoutIndex()`'s own `-1` "Model" convention. Built
from `Document::Layouts()` (`ui::LayoutsAccessibleTree`,
`src/ui/Panels.cpp`), independent of whether the Layouts panel window is
actually open on screen right now, the same way the other panel-backed
regions above don't depend on their own panel windows being open. A
screen-reader user can create a new layout and see it added entirely from
the command line (`Layout`, see `cmd_viewtools.cpp`'s `LayoutCommand`)
without needing to see the panel at all.

**Block Manager**: a "Block Manager" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per block definition, named after it, each carrying a
`Description` giving its object and instance counts as plain text (e.g.
`"2 objects, 5 instances"`) - the same two facts the on-screen Block
Manager panel's Objects/Instances columns show per row (see
`DrawBlockManagerPanel`, `cmd_drafting.cpp`); the instance count is computed
the same live way the panel itself does, by counting objects tagged
`user_text["Block"]` with that block's name, not cached. Built from
`Document::Blocks()` (`ui::BlockManagerAccessibleTree`,
`src/commands/cmd_drafting.cpp`, alongside `DrawBlockManagerPanel` itself),
independent of whether the Block Manager panel window is actually open on
screen right now, the same way the other panel-backed regions above don't
depend on their own panel windows being open. A screen-reader user can
define a block from the current selection (`Block`), insert an instance
(`Insert`), or rename one (`BlockRename`) entirely from the command line
(see `cmd_drafting.cpp`) and confirm which blocks exist and how many
instances each has without needing to see the panel at all.

**Layer State Manager**: a "Layer State Manager" accessible
(`ATSPI_ROLE_LIST`) with one `ATSPI_ROLE_LIST_ITEM` per saved layer state,
named after it, each carrying a `Description` giving how many layers it
records a visible/locked snapshot for (e.g. `"3 layers"`) - a fact the
on-screen `DrawLayerStateManager` row (a bare `Selectable` naming the state)
does not itself show, so this mirror gives a screen-reader user more than a
sighted user gets from the row alone, not less. Built from
`Document::LayerStates()` (`ui::LayerStateManagerAccessibleTree`,
`src/ui/Panels.cpp`), independent of whether the Layer State Manager panel
window is actually open on screen right now, the same way the other
panel-backed regions above don't depend on their own panel windows being
open. A screen-reader user can save and restore a named layer state entirely
from the command line (`LayerState Save <name>` / `Restore <name>` /
`Delete <name>`, see `cmd_drafting.cpp`'s `LayerStateCommand`) and confirm
what got saved without needing to see the panel at all.

**Document User Text**: a "Document User Text" accessible
(`ATSPI_ROLE_LIST`) with one `ATSPI_ROLE_LIST_ITEM` per document user-text
key, named after the key, each carrying its value as its `Description` -
the same `"key = value"` fact `DrawDocumentUserTextPanel`'s own row shows
per entry, in `Document::UserText()`'s `std::map` key order. Built from
`Document::UserText()` (`ui::DocumentUserTextAccessibleTree`,
`src/ui/Panels.cpp`), independent of whether the Document User Text panel
window is actually open on screen right now, the same way the other
panel-backed regions above don't depend on their own panel windows being
open. A screen-reader user can set, read or clear a document user-text pair
entirely from the command line (`SetDocumentUserText <key> [value]` -
omitting the value removes the key; `GetDocumentUserText` prints every pair
- see `cmd_state.cpp`/`cmd_edit.cpp`) and confirm what's stored without
needing to see the panel at all.

**Lights**: a "Lights" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per document light, its type and on/off state folded
into the name (e.g. `"Key Light (Point), on"`) - the same two facts
`DrawLightsPanel`'s own collapsed row shows without needing to expand it (the
tree-node label reads `"name (type)"`; the checkbox beside it is the on/off
state - see `RenderPanels.cpp`). Colour, intensity, position and direction
stay in the expanded editor only, the same way Materials' gloss/
reflectivity/transparency/texture stay out of its own row mirror. Built from
`Document::Lights()` (`ui::LightsAccessibleTree`, `src/ui/RenderPanels.cpp`),
independent of whether the Lights panel window is actually open on screen
right now, the same way the other panel-backed regions above don't depend on
their own panel windows being open. A screen-reader user can create a point
light and confirm it was added entirely from the command line (`PointLight`,
see `cmd_render.cpp`'s `LightCommand`) without needing to see the panel at
all.

**Annotation Styles**: an "Annotation Styles" accessible (`ATSPI_ROLE_LIST`)
with one `ATSPI_ROLE_LIST_ITEM` per style, the document's current style
(`DocumentSettings::annotation_style`) called out in its name (e.g.
`"Detail, current"`) - `DrawDocumentPropertiesWindow`'s own "Current style"
combo distinguishes it only by which entry is selected, not by text, so this
mirror spells it out the same way `BuildLayoutsPanelNode` spells out which
layout is active - and a `Description` giving its text height, arrow size
and font as plain text (0 rendered as its own documented "auto" meaning,
e.g. `"Auto (twice the grid spacing)"` rather than a bare `"0"`) - facts that
only appear once a style's own row is expanded on screen, so this mirror
gives a screen-reader user more than the collapsed on-screen list itself
shows, the same honest trade Layer State Manager's own row mirror makes.
Unlike Named Views/Named CPlanes, this list is never empty: every document
starts with a single `"Default"` style. Built from
`Document::AnnotationStyles()` (`ui::AnnotationStylesAccessibleTree`,
`src/ui/Panels.cpp`), independent of whether the Document Properties window
is actually open on screen right now, the same way the other panel-backed
regions above don't depend on their own panel windows being open. A
screen-reader user can create or edit a style and confirm it entirely from
the command line (`AnnotationStyles Name=<name> Height=<h> Arrow=<a>
Font=<f>`, see `cmd_annotate2.cpp`'s `AnnotationStylesCommand`, which also
makes the named style current unless told otherwise) without needing to see
the panel at all.

**Notes**: a single "Document Notes" accessible (`ATSPI_ROLE_LOG` - the same
role as the "Command Line" accessible, not a List) whose `Text.GetText()`
returns exactly `Document::Notes()` - the same plain string
`DrawNotesPanel`'s `##notes` multiline text box edits, matching what the
on-screen widget itself is: one editable block of free text, not a
collection of rows. Built from `Document::Notes()`
(`ui::DocumentNotesAccessibleTree`, `src/ui/Panels.cpp`), independent of
whether the Notes panel window is actually open on screen right now, the
same way the other panel-backed regions above don't depend on their own
panel windows being open. Unlike every other region added since Command
Options, there is no command-line way to set this value at all (`Notes`
just opens the panel - see `cmd_file.cpp`); a screen-reader user can still
read what's stored, but writing a note still means driving the real
multiline text box directly.

**Environments**: an "Environments" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per `"Label: value"` fact about the document's render
Environment settings - the same facts `DrawEnvironmentsPanel`'s own
"Background", "Ground plane" and "Sun and sky" sections show (background
type, plus its colour, gradient colours or image path, whichever the current
type actually uses; ground plane on/off, height, colour and shadows; sun
on/off, azimuth, altitude and skylight) - reusing the label/value row shape
the "Properties" accessible already uses for the current selection's facts,
just under its own "Environments" name rather than "Properties". The
"Gradient background in modelling views" checkbox and the render
width/height/quality stay out of this mirror, the same way Materials'
gloss/reflectivity/transparency/texture stay out of its own row mirror.
Built from `Document::Render()` (`ui::EnvironmentsAccessibleTree`,
`src/ui/RenderPanels.cpp`), independent of whether the Environments panel
window is actually open on screen right now, the same way the other
panel-backed regions above don't depend on their own panel windows being
open. Unlike Named Views/Named CPlanes, this list is never empty: every
document ships with a full set of render Environment settings already (the
default background is Sky, not the first enum value - see
`RenderSettings::background`'s own default). A screen-reader user can change
any of these entirely from the command line (`Environments Background=<type>`
/ `GroundPlane` / `Sun`, see `cmd_render.cpp`) and confirm the result without
needing to see the panel at all.

**Audit Results**: an "Audit Results" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per invalid object found by the last `Audit` run,
named after the object's id and kind (e.g. `"Object 12 (curve)"`) with
OpenNURBS' own `IsValid()` failure text as its `Description` (e.g. `"start of
NURBS knot vector is not increasing"`) - the same two facts the on-screen
Audit Results panel's Type and Problem columns show per row (see
`DrawAuditResultsPanel`). Built from `Application::AuditResults()`
(`ui::AuditResultsAccessibleTree`, `src/ui/Panels.cpp`), independent of
whether the Audit Results panel window is actually open on screen right now,
the same way the other panel-backed regions above don't depend on their own
panel windows being open. Unlike the other panel-backed regions above, this
one mirrors a snapshot from the last time a command ran, not a persisted
part of the document itself - it starts empty (no `Audit` run yet this
session) the same way Named Views/Named CPlanes start empty. A screen-reader
user can run `Audit` from the command line (see `cmd_analyze.cpp`) and see
exactly which objects it flagged and why, without needing to see the panel
at all; `MakeInvalidCurve` (test/QC-only, same file) deliberately builds one
genuinely invalid curve so this can be exercised without hand-corrupting a
real document.

**Undo History and Redo History**: an "Undo History" and a "Redo History"
accessible (both `ATSPI_ROLE_LIST`), each with one `ATSPI_ROLE_LIST_ITEM`
per pending-or-undoable (respectively redoable) edit, named `"N. label"` in
the same numbered order the on-screen `DrawUndoMultipleWindow` popup shows
for that direction (e.g. `"1. Move"`, `"2. Line"`) - so a screen-reader user
can read exactly how many steps a given Undo or Redo would need to reach any
past edit, the same information a sighted user gets by opening the Undo
Multiple / Redo Multiple window and counting rows. Built from
`Document::UndoLabels()`/`RedoLabels()` (`ui::UndoHistoryAccessibleTree`/
`RedoHistoryAccessibleTree`, `src/ui/Panels.cpp`), independent of whether
either popup window is actually open on screen right now, the same way the
other panel-backed regions above don't depend on their own panel windows
being open. Both start empty on a fresh document, the same "starts empty,
gains rows" shape Named Views/Named CPlanes and Audit Results already use;
Redo History also empties out the moment a fresh edit is made after an
Undo, matching the real Redo stack. A screen-reader user can drive either
direction entirely from the command line (`Undo`/`Redo`, see `Document.cpp`)
without needing to see either popup at all.

**Hatch Patterns**: a "Hatch Patterns" accessible (`ATSPI_ROLE_LIST`) with
one `ATSPI_ROLE_LIST_ITEM` per pattern in the loaded Hatch Pattern library,
named after the pattern (e.g. `"ANSI31"`) with its own human-readable
description text (`HatchPattern::description`, e.g. `"ANSI Iron, Brick,
Stone masonry"`, already plain text parsed straight out of the `.pat` file)
as its `Description` - the same two facts `DrawHatchPatternsPanel`'s
thumbnail grid conveys visually per pattern (the name below the thumbnail,
the description as its hover tooltip). Built from
`HatchLibrary::Instance().Patterns()` (`ui::HatchPatternsAccessibleTree`,
`src/commands/cmd_drafting2.cpp`), independent of whether the Hatch Patterns
panel window is actually open on screen right now, the same way the other
panel-backed regions above don't depend on their own panel windows being
open. Unlike the document-scoped regions above, this list is never empty
from application startup: the library always carries its built-in patterns
even with no `data/*.pat` files present, the same "never empty" shape
Environments uses. A screen-reader user can apply any pattern entirely from
the command line (`Hatch`, see `cmd_drafting2.cpp`) after selecting a
boundary, without needing to see the thumbnail grid at all.

**Plug-ins**: a "Plug-ins" accessible (`ATSPI_ROLE_LIST`) with one
`ATSPI_ROLE_LIST_ITEM` per loaded plug-in, named after its name, version and
Loaded/Error status (e.g. `"Fillet Helper 1.2, Loaded"`) with a `Description`
giving its command and Dino Flow node counts (plus the load error text when
it failed to load) - the same facts `DrawPlugInManagerPanel`'s
Name/Version/Commands/Flow Nodes/Status table columns show per row, its
Status column's hover-tooltip error text folded directly into the row's
`Description` since AT-SPI has no per-cell tooltip to mirror it into. Built
from `plugins::Manager::Get().Plugins()` (`plugins::PluginsAccessibleTree`,
`src/plugins/PluginPanel.cpp`), independent of whether the Plug-in Manager
panel window is actually open on screen right now, the same way the other
panel-backed regions above don't depend on their own panel windows being
open. Not empty by default in a real build: `Application`'s own constructor
runs `Manager::ScanDefaultFolders` at startup (`app/Application.cpp`), so
any plug-in already sitting in the config/exe-dir plugins folders -
including this repository's own bundled `mesh_tools`/`curve_tools`/
`analysis_tools`/`sample` example plug-ins - is already loaded and listed
here before a screen-reader user does anything, the same "never empty"
shape Hatch Patterns uses above, not the "starts empty, gains rows" shape
Named Views/Named CPlanes and Audit Results use. The panel's own "Load
File..."/"Rescan Folders" buttons add further entries after that.

**Why these regions and not the rest of the UI**: the command line is the
one region where "expose the text" is both sufficient (there is no
meaningful spatial layout to convey - it *is* a stream of text) and
complete on its own (every command in the ~1000+ catalog is already
reachable by typing into it, per section 2). The menu bar, the
Layers/Properties panels, the viewports, the Activity Log, Named Views,
Named CPlanes, Linetypes, Materials, Clipping Planes, Layouts, Block
Manager, Layer State Manager, Document User Text, Lights, Annotation Styles,
Notes, Environments, Audit Results, Undo/Redo History, Hatch Patterns and
Plug-ins extend this to the
next-most load-bearing UI surfaces - discovering what commands exist by
name, inspecting/editing layer and object state, knowing where you're
looking, reviewing what actually happened to the document, recalling a
saved camera bookmark, recalling a saved construction plane, knowing which
dash patterns and materials are available to apply, knowing which clipping
planes exist and whether each is on, knowing which layout is currently
active, knowing which block definitions exist and how many instances of
each are placed, recalling a saved layer state, knowing what document
user-text metadata is stored, knowing which lights exist and whether each is
on, knowing which annotation style is current and its text height/arrow
size/font, reading the document's free-text Notes, knowing the render
Environment's background/ground plane/sun and sky settings, reviewing which
objects the last Audit run found invalid and why, knowing how many steps an
Undo or Redo would take to reach a given past edit, knowing which hatch
patterns are available to apply, and knowing which plug-ins are loaded and
whether each loaded successfully - without requiring
the full
shadow-tree-for-every-widget effort described above.
Mirroring the 3D viewport and the ~22 remaining panels/dialogs the same way
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
- Named Views has the same read-only gap (no `Action` interface - restoring
  or deleting a saved view over AT-SPI itself is not possible; a
  screen-reader user still drives that through the equivalent `NamedView
  Restore <name>`/`Delete <name>` command by name), and, matching
  `DrawNamedViewsPanel` itself, exposes only each view's name - not its
  saved camera location/target, which stays queryable only via `NamedView
  List` on the command line.
- Named CPlanes has the same read-only gap (no `Action` interface - restoring
  or deleting a saved construction plane over AT-SPI itself is not possible;
  a screen-reader user still drives that through the equivalent `NamedCPlane
  Restore <name>`/`Delete <name>` command by name), and, matching
  `DrawNamedCPlanesPanel` itself, exposes only each plane's name - not its
  saved origin/x-axis/y-axis, which stays queryable only via `NamedCPlane
  List` on the command line.
- Linetypes has the same read-only gap (no `Action` interface - applying a
  linetype to the current selection or layer over AT-SPI itself is not
  possible; a screen-reader user still drives that through the equivalent
  `SetLinetype Name=<name>`/`SetLayerLinetype Name=<name>` command by name),
  and it does not expose the document's linetype scale or on/off display
  setting (`DocumentSettings::linetype_scale`/`linetype_display`), which
  `DrawLinetypesPanel` shows above its table - those stay queryable only via
  `SetLinetypeScale`/`LinetypeDisplay` on the command line.
- Materials has the same read-only gap (no `Action` interface - assigning a
  material to the current selection over AT-SPI itself is not possible; a
  screen-reader user still drives that through the equivalent
  `RenderAssignMaterialToObjects` command by name), and it exposes only the
  diffuse colour per row - gloss, reflectivity, transparency, emission and
  texture stay queryable only via `DrawMaterialsPanel`'s own expanded editor
  or by reading the material back out through a script.
- Clipping Planes has the same read-only gap (no `Action` interface -
  switching a plane on/off or restricting it to specific viewports over
  AT-SPI itself is not possible; a screen-reader user still drives that
  through the equivalent `EnableClippingPlane`/`DisableClippingPlane`
  command by name), and it does not expose each plane's origin, size or
  normal - matching `DrawClippingPlanesPanel` itself, which also keeps those
  as a hover tooltip rather than part of the row - those stay queryable only
  via the panel itself or a script.
- Layouts has the same read-only gap (no `Action` interface - switching the
  active layout over AT-SPI itself is not possible; a screen-reader user
  still drives that through the equivalent `Layouts <name>` command by
  name), and it exposes only the name and active state per row - matching
  `DrawLayoutsPanel`'s own top-level row - not the page size or per-detail
  state shown only once a row is expanded, which stay queryable only via
  `LayoutProperties` or `Layouts` (with no name, to print every layout's
  size and detail count) on the command line.
- Block Manager has the same read-only gap (no `Action` interface -
  selecting a block's instances, renaming it, deleting it or inserting a
  new instance over AT-SPI itself is not possible; a screen-reader user
  still drives that through the equivalent `SelBlockInstanceOf`/
  `BlockRename`/`Purge`/`Insert` commands by name), and it exposes only the
  object/instance counts per row - not each object's own geometry, which
  stays queryable only by selecting the instance and reading Properties.
- Layer State Manager has the same read-only gap (no `Action` interface -
  restoring or deleting a saved state over AT-SPI itself is not possible; a
  screen-reader user still drives that through the equivalent `LayerState
  Restore <name>`/`Delete <name>` command by name), and it exposes only the
  state's name and how many layers it snapshots - not which layers or what
  their saved visible/locked values are, which stay queryable only via
  `LayerState` (with no name, to list every saved state) on the command
  line.
- Document User Text has the same read-only gap (no `Action` interface -
  setting or clearing a key over AT-SPI itself is not possible; a
  screen-reader user still drives that through the equivalent
  `SetDocumentUserText <key> [value]` command), and, unlike every other
  region above, this one has no cap on value length either - a very long
  value is carried in full as the row's `Description`, same as Activity
  Log's own no-cap note above.
- Lights has the same read-only gap (no `Action` interface - switching a
  light on/off, changing its colour/intensity or moving it over AT-SPI
  itself is not possible; a screen-reader user still drives that through the
  Lights panel or the equivalent `PointLight`/`SpotLight`/
  `DirectionalLight`/`RectangularLight`/`LinearLight` command by name), and
  it exposes only the name, type and on/off state per row - not colour,
  intensity, position or direction, which stay queryable only via the
  expanded panel editor.
- Annotation Styles has the same read-only gap (no `Action` interface -
  switching the document's current style over AT-SPI itself is not
  possible; a screen-reader user still drives that through the equivalent
  `AnnotationStyles Name=<name>` command), and `GetState` aside, it is the
  one region above where 0 in the underlying data (`text_height`/
  `arrow_size`) is deliberately *not* surfaced as a bare `"0"` - see
  `AnnotationStyleSummary`'s own comment for why that would misread as a
  literal zero rather than "auto".
- Notes is the one region with no read-only gap to note, because it has no
  gap the other way either: unlike every other region above, there is no
  command-line path to *write* this value at all, only to read it (`Notes`
  just opens the panel - see `cmd_file.cpp`'s registration and this
  region's own writeup above).
- Undo History and Redo History have the same read-only gap as the other
  list regions above (no `Action` interface - jumping straight to a given
  numbered entry over AT-SPI itself is not possible, only reading how many
  steps away it is; a screen-reader user still drives that through the
  equivalent number of `Undo`/`Redo` command invocations, or the on-screen
  popup's own click-a-row shortcut), and, matching `DrawUndoMultipleWindow`
  itself, exposes only each entry's label - not what objects or properties
  it actually touched, which stays queryable only via `List`/`What` on the
  objects themselves after the fact.
- Hatch Patterns has the same read-only gap (no `Action` interface -
  applying a pattern to the current selection over AT-SPI itself is not
  possible; a screen-reader user still drives that through the equivalent
  `Hatch Pattern=<name>` command by name), and it does not expose each
  pattern's line-family geometry (angle/spacing/dash arrays) - only its name
  and description text, matching what the on-screen thumbnail conveys
  without hovering for the tooltip.
- Plug-ins has the same read-only gap (no `Action` interface - loading,
  unloading or enabling/disabling a plug-in over AT-SPI itself is not
  possible; a screen-reader user still drives that through the Plug-in
  Manager panel's own Load File.../Rescan Folders buttons, since there is no
  command-line equivalent), and it does not expose each plug-in's registered
  command/flow-node *names* - only their counts, matching what
  `DrawPlugInManagerPanel`'s own collapsed table row shows without expanding
  it further (there is no further expansion on screen either).

**Internal design, independent of AT-SPI itself**: the accessible tree's
*shape and text* are built by a small, pure, platform-independent module,
`src/platform/AccessibilityTree.h`/`.cpp` (`BuildAccessibleTree`,
`BuildCommandLineText`, `MenuTreeBuilder`, `BuildLayersPanelNode`,
`BuildPropertiesPanelNode`, `BuildCommandOptionsNode`,
`BuildViewportsPanelNode`, `BuildActivityLogNode`, `BuildNamedViewsNode`,
`BuildNamedCPlanesNode`, `BuildLinetypesNode`, `BuildMaterialsPanelNode`,
`BuildClippingPlanesPanelNode`, `BuildLayoutsPanelNode`, `BuildBlockManagerNode`,
`BuildLayerStateManagerNode`, `BuildDocumentUserTextNode`, `BuildLightsPanelNode`,
`BuildAnnotationStylesNode`, `BuildDocumentNotesNode`, `BuildEnvironmentsPanelNode`,
`BuildAuditResultsNode`, `BuildUndoHistoryNode`, `BuildRedoHistoryNode`,
`BuildHatchPatternsNode`, `BuildPluginsNode`), with its own unit test
(`tests/test_accessibility_tree.cpp`, registered as the
`dino8_accessibility_tree` CTest target) that needs no display, no D-Bus, and
no AT-SPI2 build at all - it runs on every platform and every CI job. The
menu bar's mirror is recorded live, right alongside the real ImGui
menu-drawing calls in `src/ui/MenuBar.cpp` (`MenuTreeBuilder`'s
`OpenMenu`/`CloseMenu`/`LeafMenu`/`Item`, driven by that file's
`BeginMenuA`/`EndMenuA`/`MenuItemA`/`Item` wrappers), so it can never drift
from what was actually drawn. The Layers, Properties, Command Options,
Viewports, Activity Log, Named Views, Named CPlanes, Linetypes, Materials,
Clipping Planes, Layouts, Layer State Manager, Document User Text and
Annotation Styles mirrors
(`src/ui/Panels.cpp`'s `LayersPanelAccessibleTree`/
`PropertiesPanelAccessibleTree`/`CommandOptionsAccessibleTree`/
`ViewportsAccessibleTree`/`ActivityLogAccessibleTree`/
`NamedViewsAccessibleTree`/`NamedCPlanesAccessibleTree`/
`LinetypesAccessibleTree`/`ClippingPlanesAccessibleTree`/
`LayoutsAccessibleTree`/`LayerStateManagerAccessibleTree`/
`DocumentUserTextAccessibleTree`/`AnnotationStylesAccessibleTree`, plus the
single-Text-value `DocumentNotesAccessibleTree`, plus `UndoHistoryAccessibleTree`/
`RedoHistoryAccessibleTree` (built from `Document::UndoLabels()`/`RedoLabels()`,
alongside `DrawUndoMultipleWindow` itself), `src/ui/RenderPanels.cpp`'s
`MaterialsAccessibleTree`, `LightsAccessibleTree` and `EnvironmentsAccessibleTree`,
`src/commands/cmd_drafting.cpp`'s
`BlockManagerAccessibleTree`, alongside `DrawBlockManagerPanel` itself,
`src/commands/cmd_drafting2.cpp`'s `HatchPatternsAccessibleTree` (built from
the global `HatchLibrary::Instance()` singleton rather than `Document`,
alongside `DrawHatchPatternsPanel` itself), and `src/plugins/PluginPanel.cpp`'s
`PluginsAccessibleTree` (built from the global `plugins::Manager::Get()`
singleton, alongside `DrawPlugInManagerPanel` itself) are
built straight from `Document`/`Application`/`CommandEngine` state (or, for
Hatch Patterns and Plug-ins, their own global singleton), independent of
`DrawLayersPanel`/`DrawPropertiesPanel`/`DrawCommandLine`/`Viewport::DrawUI`/
`DrawActivityLogPanel`/`DrawNamedViewsPanel`/`DrawNamedCPlanesPanel`/
`DrawLinetypesPanel`/`DrawMaterialsPanel`/`DrawClippingPlanesPanel`/
`DrawLayoutsPanel`/`DrawBlockManagerPanel`/`DrawLayerStateManager`/
`DrawDocumentUserTextPanel`/`DrawLightsPanel`/
`DrawDocumentPropertiesWindow`/`DrawNotesPanel`/`DrawUndoMultipleWindow`/
`DrawHatchPatternsPanel`/`DrawPlugInManagerPanel`. `src/ui/Panels.cpp`'s own
`AuditResultsAccessibleTree` (built from `Application::AuditResults()`,
alongside `DrawAuditResultsPanel` itself) follows the same shape.
`AccessibilityLinux.cpp` is a thin transport on top
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
reporting itself active), "Activity Log", "Named Views", "Named CPlanes",
"Linetypes", "Materials", "Clipping Planes", "Layouts", "Block Manager",
"Layer State Manager", "Document User Text", "Lights", "Annotation Styles"
and "Document Notes" accessibles - the
same objects a screen reader would find - then asserts the command line's
and the Properties list's content each change
after a real command (`Line 0,0,0 10,10,0`) runs, that Command Options goes
empty -> lists Circle's option chips -> empty again around a real running
`Circle` command, that the Activity Log gains a new entry naming `Line`
right after that same command finishes, that Named Views starts empty and
gains an entry named `"MyView"` right after a real `NamedView Save MyView`
command runs, that Named CPlanes starts empty and gains an entry named
`"MyCPlane"` right after a real `NamedCPlane Save MyCPlane` command runs,
that Linetypes starts with the built-in "Continuous" linetype already
present and gains an entry named `"MyLinetype"` right after a real
`SetCustomLinetype Name=MyLinetype Pattern=5,2` command runs, that Materials
starts empty (a fresh document has no built-in materials), that Clipping
Planes starts empty and gains an entry reporting itself "on" right after a
real `ClippingPlane 0,0,0 5,5,0` command runs, that Layouts starts empty
and gains an entry named `"MyLayout, active"` right after a real `Layout
MyLayout` command runs, that Block Manager starts empty and gains an entry
named `"MyBlock"` (carrying its object/instance counts as its Description)
right after the already-created objects are selected (`SelAll`) and turned
into a block (`Block` / a base point / a name), that Layer State Manager
starts empty and gains an entry named `"MyLayerState"` right after a real
`LayerState Save MyLayerState` command runs, that Document User Text
starts empty and gains an entry named `"MyKey"` with `"MyValue"` as its
Description right after a real `SetDocumentUserText MyKey MyValue` command
runs, that Lights starts empty and gains an entry named `"MyLight (Point),
on"` right after a real `PointLight Name=MyLight 0,0,10` command runs, that
Annotation Styles starts with the built-in "Default" style already present
and named current and gains an entry named `"MyStyle, current"` (carrying
its text height/arrow size/font as its Description) right after a real
`AnnotationStyles Name=MyStyle Height=2.5 Arrow=1 Font=Arial` command runs,
and that Document Notes reports the empty string in a fresh document (no
mutation check: there is no command-line way to set it). It does not fake,
mock, or stub any part of the AT-SPI2 stack.

This was run successfully, including the new Lights/Annotation Styles/
Document Notes checks, in the environment this addition was built and
verified in, after installing:
`libatspi2.0-dev`, `libglib2.0-dev`, `at-spi2-core` (provides
`at-spi2-registryd`), `dbus-x11` (provides `dbus-daemon`), `xvfb`, and
`python3-pyatspi` (Ubuntu 24.04/noble package names) - all checks
above passed against the real registry daemon, including "Command Options
lists Circle's option chips while it is running (['Diameter', '3Point',
'Vertical'])", "Lights gains a new entry naming, typing and stating the new
light once \"PointLight\" runs ('MyLight (Point), on')", and "Annotation
Styles gains a new, current entry with its text height/arrow size/font as
its Description once \"AnnotationStyles\" runs ('MyStyle, current', 'Text
height: 2.5; Arrow size: 1; Font: Arial')". One environment-specific wrinkle worth knowing about, not
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
| Screen-reader support (command line, menu bar, command options, Layers/Properties panels, viewports, Activity Log, Named Views, Named CPlanes, Linetypes, Materials, Clipping Planes, Layouts, Block Manager, Layer State Manager, Document User Text, Lights, Annotation Styles, Notes, Environments, Audit Results, Undo/Redo History, Hatch Patterns, Plug-ins) | Shipped on Linux: a real AT-SPI2 bridge (`src/platform/AccessibilityLinux.cpp`) exposes the command line's live text and full history log, the main menu bar (mirroring exactly what's currently open, built live alongside `ui/MenuBar.cpp`'s own drawing calls), the running command's options (with per-option guidance on how to change it), the Layers/Properties panels' current content (Properties rows note which are real value editors), every viewport's title/view-menu button state (name, active/maximized, current display mode), the persisted Activity Log of finalized edits (timestamp/action/summary per entry, distinct from the command line's own raw text log), the document's saved Named Views (name per saved view), its saved Named CPlanes (name per saved construction plane), its Linetypes (name and dash pattern per linetype), its Materials (name and diffuse colour per material), its Clipping Planes (name, on/off state and viewport scope per plane), its Layouts (name per layout, with the active one called out), its Block Manager (name plus object/instance counts per block definition), its Layer State Manager (name plus layer count per saved state), its Document User Text (key/value per document user-text entry), its Lights (name, type and on/off state per light), its Annotation Styles (name and current-style flag per style, plus text height/arrow size/font as Description), its Notes (the document's free-text Notes, as a single Text value), its render Environment settings (background/ground plane/sun and sky, as Label: value rows), the last Audit run's results (id/type per invalid object, with the failure reason as Description), its pending Undo/Redo history (numbered label per pending-or-undoable/redoable edit), the loaded Hatch Pattern library (name and description per pattern), and its loaded plug-ins (name/version/status plus command and flow-node counts per plug-in) as queryable, updating accessible objects, verified end-to-end against the real registry daemon and `pyatspi` (`tests/smoke_accessibility.py`), including the new Undo/Redo History/Hatch Patterns/Plug-ins checks. Windows/macOS not implemented. Built only when `atspi-2`/`gio-2.0` are available; a silent no-op otherwise |
| Screen-reader support (rest of the UI) | Not implemented - hard ImGui platform limitation (no accessibility-tree bridge for the 3D viewport's own rendered content or the ~22 remaining panels/dialogs on any OS). Descriptive text/tooltips exist everywhere as a prerequisite, but that is not screen-reader support |
