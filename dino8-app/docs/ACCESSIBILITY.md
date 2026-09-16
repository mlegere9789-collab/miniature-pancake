# Accessibility in Dino 8

This document is an honest account of what Dino 8's UI does and does not
support for users with disabilities, given that the UI is built on
[Dear ImGui](https://github.com/ocornut/imgui). It covers three areas: a
high-contrast theme (shipped), full keyboard operability (audited and
fixed where it was broken), and screen-reader support (a hard platform
limitation of ImGui itself - not shipped, and not something a few labels
can fix).

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

## 3. Screen-reader support - not implemented, and honestly can't be with a few labels

**Dear ImGui has no built-in accessibility-tree integration.** It draws
every widget as textured triangles into a single OpenGL/Vulkan/DirectX
framebuffer; there is no retained widget tree, no OS-level control
handles, and nothing for a screen reader to attach to. This is a
documented, structural limitation of the library, not a Dino 8-specific
gap:

- **Windows**: no UI Automation (UIA) or MSAA integration. A screen reader
  like NVDA or Narrator sees an opaque window with no children.
- **macOS**: no `NSAccessibility` integration. VoiceOver sees the same
  opaque window.
- **Linux**: no AT-SPI integration. Orca sees the same opaque window.

Wiring any of the above up is a major, platform-specific undertaking that
Dear ImGui itself does not attempt and has no supported extension point
for: it means either (a) maintaining a shadow accessibility tree that
mirrors every ImGui window/widget per frame and pushing it through each
platform's native accessibility API, which the ImGui maintainers have
discussed for years without landing, or (b) replacing ImGui's rendering
for accessibility-tree purposes with a parallel native-widget layer, which
would be a different UI toolkit in practice. Neither is a "few labels"
fix, and claiming screen-reader support without one of those would be
false.

**What Dino 8 does have that is a real, if partial, prerequisite for any
future screen-reader work**: every interactive control in the UI already
carries non-empty, descriptive text that ImGui associates with it - either
as the control's own visible label (menu items, buttons with captions,
panel titles) or, for icon-only controls (toolbar/sidebar tool buttons),
as the tooltip text shown on hover *and, as of the keyboard-nav fix above,
on keyboard focus too* (`RichTooltip` in `src/ui/Toolbars.cpp`, sourced
from the command catalog's name/description or the button's built-in
fallback string). There is no silent icon-only button anywhere in the
audited UI that has no text fallback at all. This is necessary scaffolding
if ImGui or a Dino 8 fork ever grows real accessibility-tree support (that
work would consume exactly this text), but it is not sufficient on its own
and is not "screen reader support" - without an accessibility-tree bridge,
none of this text reaches any screen reader today.

## Summary for RHINO8_KILLER_AUDIT.md

| Area | Status |
|---|---|
| High-contrast theme | Shipped: Options > General > Theme > High Contrast |
| Keyboard-only operability | Audited; one real bug found and fixed (toolbar/sidebar/tab-strip/bell/viewport-title buttons were `InvisibleButton` without `EnableNav`, so Tab skipped them); nav-focus tooltips added for icon-only buttons; free 3D viewport orbit and a few inherently-drag widgets remain mouse-only by design, same as in Rhino |
| Screen-reader support | Not implemented - hard ImGui platform limitation (no accessibility-tree bridge on any OS). Descriptive text/tooltips exist everywhere as a prerequisite, but that is not screen-reader support |
