#!/usr/bin/env python3
"""End-to-end test for the AT-SPI2 accessibility bridge.

See src/platform/AccessibilityLinux.cpp and docs/ACCESSIBILITY.md. Spawns
the real Dino8 binary under --smoke and queries its live AT-SPI2 accessible
tree - through the real AT-SPI2 registry daemon, over a real D-Bus
connection, the same way a screen reader would - to prove:

  1. Dino8 registers a "Command Line" accessible text region that is
     actually discoverable by walking the AT-SPI2 desktop (not merely
     present somewhere in Dino8's own memory).
  2. Its Text interface's GetText() is queryable before any command runs.
  3. Running one real command through the app changes that value to
     include the command that ran (not just "the app didn't crash").
  4. A "Menu Bar" accessible (role MENU_BAR) is also discoverable, with a
     "File" child (role MENU) mirroring the real top-level menu - present
     even though nothing is open in this headless run (see
     ui/MenuBar.cpp's MenuTreeBuilder use: a closed menu records its name
     with no children, honestly reflecting "not open" rather than faking
     item content that was never actually drawn).
  5. A "Layers" accessible (role LIST) is discoverable with one ListItem
     for the document's initial "Default" layer.
  6. A "Properties" accessible (role LIST) is discoverable, and its content
     changes - like the command line's - after a real command creates an
     object (the same before/after pattern as check 3, over a different
     accessible).
  7. A "Command Options" accessible (role LIST) is discoverable, is empty
     while no command is running, gains one ListItem per option (e.g.
     "Diameter", "3Point", "Vertical") the moment a real command that offers
     some (Circle) starts running, and goes back to empty once that command
     finishes - mirroring the option chips Application.cpp's DrawCommandLine
     draws next to the prompt (see Command.h's OptionSpec).
  8. A "Viewports" accessible (role LIST) is discoverable, with one ListItem
     per viewport (including the default "Perspective" view, named "active"
     since it starts as the focused viewport) - mirroring each viewport's
     title/view-menu button and corner display-mode label (see
     Viewport.cpp's title-overlay block).
  9. An "Activity Log" accessible (role LIST) is discoverable, and gains one
     new ListItem naming the "Line" command once a second, otherwise
     uninteresting edit finalizes it - mirroring Document::ActivityLog(), the
     persisted, structured record of every finalized edit (see
     Document::RecordActivityLogEntry), which is distinct from the command
     line's own raw text log. An edit's own completion does not finalize it
     into this log by itself: Document::FinalizePending() only runs when the
     *next* Document::BeginChange() fires (see Document.cpp), so this check
     - like the Command Options and command-line/Properties checks above -
     drives one more real command through the app to observe it.
  10. A "Named Views" accessible (role LIST) is discoverable, starts empty,
      and gains one new ListItem named after the view once a real
      "NamedView Save <name>" command runs - mirroring Document::NamedViews(),
      the same way the Activity Log check above observes a real edit landing
      in its own accessible.
  11. A "Named CPlanes" accessible (role LIST) is discoverable, starts empty,
      and gains one new ListItem named after the construction plane once a
      real "NamedCPlane Save <name>" command runs - mirroring
      Document::NamedCPlanes(), the same before/after pattern check 10 uses
      for Named Views.
  12. A "Linetypes" accessible (role LIST) is discoverable, starts with the
      document's built-in linetypes (at least "Continuous"), and gains one
      new ListItem named after a custom linetype once a real
      "SetCustomLinetype Name=<name> Pattern=<pattern>" command runs -
      mirroring Document::Linetypes(), the same before/after pattern checks
      10/11 use for Named Views/Named CPlanes (Linetypes just starts
      non-empty, since unlike named views/cplanes every document ships with
      built-in linetypes already - see Document::DefaultLinetypes()).
  13. A "Materials" accessible (role LIST) is discoverable and starts empty
      (a fresh document has none - unlike Linetypes it ships with no
      built-ins) - mirroring Document::Materials(). No mutation check here:
      unlike NamedView/NamedCPlane/SetCustomLinetype, adding a material over
      the command line (RenderAssignMaterialToObjects) requires a real
      object selection to finish, which this script does not set up.
  14. A "Clipping Planes" accessible (role LIST) is discoverable, starts
      empty, and gains one new ListItem reporting itself "on" once a real
      "ClippingPlane <corner1> <corner2>" command runs - mirroring
      Document::ClippingPlanes(), the same before/after pattern check 10
      uses for Named Views.
  15. A "Layouts" accessible (role LIST) is discoverable, starts empty, and
      gains one new ListItem named after the layout and reporting itself
      "active" once a real "Layout <name>" command runs (LayoutCommand
      makes the new layout active - see cmd_viewtools.cpp) - mirroring
      Document::Layouts(), the same before/after pattern check 10 uses for
      Named Views.
  16. A "Block Manager" accessible (role LIST) is discoverable, starts
      empty, and gains one new ListItem named after a new block definition,
      carrying its object/instance counts as its Description, once the
      already-created objects are selected ("SelAll") and turned into a
      block ("Block" / a base point / a name, the same multi-line dance
      BlockCommand itself uses - see cmd_drafting.cpp) - mirroring
      Document::Blocks(), the same before/after pattern check 10 uses for
      Named Views.
  17. A "Layer State Manager" accessible (role LIST) is discoverable, starts
      empty, and gains one new ListItem named after a saved layer state,
      carrying its layer count as its Description, once a real "LayerState
      Save <name>" command runs (see cmd_drafting.cpp's LayerStateCommand) -
      mirroring Document::LayerStates(), the same before/after pattern
      check 10 uses for Named Views.
  18. A "Document User Text" accessible (role LIST) is discoverable, starts
      empty, and gains one new ListItem named after a document user-text
      key, carrying its value as its Description, once a real
      "SetDocumentUserText <key> <value>" command runs (see cmd_state.cpp) -
      mirroring Document::UserText(), the same before/after pattern check
      10 uses for Named Views.
  19. A "Lights" accessible (role LIST) is discoverable, starts empty, and
      gains one new ListItem named after a new point light - with its type
      and "on" state folded into the name - once a real "PointLight
      Name=<name> <point>" command runs (see cmd_render.cpp's LightCommand)
      - mirroring Document::Lights(), the same before/after pattern check
      10 uses for Named Views.
  20. An "Annotation Styles" accessible (role LIST) is discoverable, starts
      with the document's single built-in "Default" style already present
      and named current (unlike Named Views/CPlanes, every document ships
      with one annotation style already - see Document's own
      AnnotationStyles() default), and gains one new ListItem naming a new
      style as current, with its text height/arrow size/font as its
      Description, once a real "AnnotationStyles Name=<name> Height=<h>
      Arrow=<a> Font=<f>" command runs (see cmd_annotate2.cpp's
      AnnotationStylesCommand, which also makes the new style current) -
      mirroring Document::AnnotationStyles().
  21. A "Document Notes" accessible (role LOG, the same role as the Command
      Line accessible) is discoverable and its Text interface reports the
      empty string in a fresh document - mirroring Document::Notes(). No
      mutation check here: unlike NamedView/NamedCPlane/SetCustomLinetype,
      there is no command-line way to set Document::Notes() (only
      DrawNotesPanel's own multiline text box writes it - see
      ui/Panels.cpp), the same "discoverable, no mutation check" shape
      check 13 uses for Materials.
  22. An "Environments" accessible (role LIST) is discoverable, starts with
      the document's default render Environment facts already present (the
      background defaults to "Sky", not empty - unlike Named Views/CPlanes,
      every document ships with a full RenderSettings already), and its
      "Background: ..." row changes to "Background: Solid" once a real
      "Environments Background=Solid" command runs - mirroring
      Document::Render() (see cmd_render.cpp's EnvironmentsCommand).
  23. An "Audit Results" accessible (role LIST) is discoverable, starts
      empty, and gains one new ListItem naming the object's id and type
      with its failure reason as the Description once a real
      "MakeInvalidCurve" then "Audit" pair of commands runs (see
      cmd_analyze.cpp) - mirroring Application::AuditResults(), the same
      before/after pattern check 10 uses for Named Views.
  24. An "Undo History" accessible (role LIST) is discoverable, starts
      empty, and after the first real "Line" command runs shows two
      numbered entries - "1. Line" (this edit, still pending) and
      "2. Circle" (the earlier Circle, finalized when this Line's own
      BeginChange fired) - mirroring Document::UndoLabels() and the same
      numbered rows DrawUndoMultipleWindow(app, /*redo=*/false) shows.
  25. A "Redo History" accessible (role LIST) is discoverable, starts
      empty, and gains a "1. MakeInvalidCurve" entry - the same label Undo
      History's own top entry loses - once a real "Undo" command runs
      after "MakeInvalidCurve"/"Audit" (check 23) - mirroring
      Document::RedoLabels() and DrawUndoMultipleWindow(app, /*redo=*/true).
  26. A "Hatch Patterns" accessible (role LIST) is discoverable and starts
      non-empty with the built-in "SOLID" pattern first - mirroring
      HatchLibrary::Instance().Patterns(), which always carries its
      built-ins even with no data/*.pat files present (unlike Named
      Views/CPlanes, the same "never empty" shape check 22 uses for
      Environments). No mutation check here: applying a pattern
      (cmd_drafting2.cpp's Hatch command) needs a real boundary selection
      first, which this script does not set up, the same reason check 13
      skips a mutation check for Materials.
  27. A "Plug-ins" accessible (role LIST) is discoverable and starts
      non-empty: Application's own constructor scans the binary's own
      exe_dir/plugins folder at startup (Manager::ScanDefaultFolders), and
      this repository builds and installs its bundled example plug-ins
      (mesh_tools, curve_tools, analysis_tools, sample) right there by
      default - so this checks only that each already-loaded entry's name
      is non-empty and reports itself Loaded/Error, not a specific count or
      order (directory iteration order is filesystem-dependent) - mirroring
      plugins::Manager::Get().Plugins(). No further mutation check here:
      loading an additional plug-in needs a real, separate shared-library
      file on disk, which this script does not provide.

This is a real integration test: at-spi2-registryd is the actual daemon
GNOME uses, pyatspi is the actual library screen readers use, and Dino8 is
the actual built binary, run headless under Xvfb exactly the way
tests/smoke.sh runs it.

Synchronization: Dino8's smoke-script mini-language has a `waitfile PATH`
directive (see main.cpp) that pauses the script - not the AT-SPI bridge,
which keeps answering queries while paused - until this test creates a
file at PATH. That makes "read the value before the command, run the
command, read the value again" deterministic, instead of racing this
script's AT-SPI polling against however fast an unattended --smoke run
happens to execute its script on a given machine.

Requires (see docs/ACCESSIBILITY.md "Verifying it yourself"):
  - Dino8 built with DINO8_HAVE_ATSPI (libatspi2.0-dev + libglib2.0-dev
    present at configure time - see CMakeLists.txt; cmake's configure-time
    log says which).
  - at-spi2-core (at-spi2-registryd) and dbus-x11 (dbus-daemon) installed.
  - Xvfb (the app still opens a real, if invisible, GLFW/X11 window under
    --smoke).
  - A Python interpreter whose `gi`/pyatspi bindings actually import. On
    Debian/Ubuntu, python3-pyatspi's compiled `gi._gi` extension targets
    one specific CPython ABI; if THIS interpreter can't import it, find the
    one that can (e.g. `python3.12 -c "import pyatspi"`) and re-run with
    that interpreter explicitly - this script fails fast with that
    instruction rather than silently skipping.

Usage: python3 tests/smoke_accessibility.py /path/to/Dino8
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

FAILURES = 0


def fail(msg):
    global FAILURES
    print(f"FAIL {msg}")
    FAILURES += 1


def ok(msg):
    print(f"ok   {msg}")


def die(msg):
    print(f"FAIL {msg}")
    sys.exit(1)


def require_tool(name):
    if shutil.which(name) is None:
        die(f"required tool not found on PATH: {name}")


def find_registryd():
    for candidate in ("at-spi2-registryd", "/usr/libexec/at-spi2-registryd",
                       "/usr/lib/at-spi2-core/at-spi2-registryd"):
        if shutil.which(candidate) or os.path.isfile(candidate):
            return candidate
    return None


def main():
    if len(sys.argv) < 2:
        die("usage: smoke_accessibility.py /path/to/Dino8")
    binary = os.path.abspath(sys.argv[1])
    if not os.path.isfile(binary):
        die(f"binary not found: {binary}")

    try:
        import gi  # noqa: F401
        gi.require_version("Atspi", "2.0")
        import pyatspi
    except Exception as exc:
        die(
            "could not import pyatspi/gi in this interpreter "
            f"({sys.executable}): {exc}\nRe-run with the interpreter whose "
            "compiled gi extension actually matches (see this file's module "
            "docstring for how to find it), e.g.:\n"
            "  python3.12 tests/smoke_accessibility.py " + binary
        )

    registryd = find_registryd()
    if registryd is None:
        die("at-spi2-registryd not found (install at-spi2-core)")
    require_tool("dbus-daemon")
    require_tool("Xvfb")

    tmp = tempfile.mkdtemp(prefix="dino8_a11y_")
    sync0 = os.path.join(tmp, "sync0")
    sync1 = os.path.join(tmp, "sync1")
    sync2 = os.path.join(tmp, "sync2")
    sync2b = os.path.join(tmp, "sync2b")
    sync3 = os.path.join(tmp, "sync3")
    sync4 = os.path.join(tmp, "sync4")
    sync5 = os.path.join(tmp, "sync5")
    sync6 = os.path.join(tmp, "sync6")
    sync7 = os.path.join(tmp, "sync7")
    sync8 = os.path.join(tmp, "sync8")
    sync9 = os.path.join(tmp, "sync9")
    sync10 = os.path.join(tmp, "sync10")
    sync11 = os.path.join(tmp, "sync11")
    sync12 = os.path.join(tmp, "sync12")
    sync13 = os.path.join(tmp, "sync13")
    sync14 = os.path.join(tmp, "sync14")
    sync15 = os.path.join(tmp, "sync15")
    sync_final = os.path.join(tmp, "sync_final")
    script_path = os.path.join(tmp, "script.txt")
    with open(script_path, "w") as f:
        # `@waitfile` (like the built-in `@wait N` frames directive) needs
        # its `@` prefix to be recognized as a script directive at all - a
        # bare `waitfile ...` line is otherwise just fed straight to
        # CommandEngine::Execute() as an (unknown) command and the script
        # keeps going immediately, which is exactly the race this
        # synchronization exists to avoid.
        #
        # "Circle 0,0,0" starts Circle and feeds it a center point, leaving
        # it running and waiting for a radius (Command::options is populated
        # from Begin() unconditionally - see cmd_create.cpp's CircleCommand -
        # so the Diameter/3Point/Vertical chips are already live at this
        # point); "5" then feeds the radius, finishing it. This is what lets
        # the test observe the Command Options accessible go
        # empty -> populated -> empty around a real running command, the
        # same way checks 3/6 already observe the command line/Properties
        # change around Line.
        f.write(f"@waitfile {sync0}\n")
        f.write("Circle 0,0,0\n")
        f.write(f"@waitfile {sync1}\n")
        f.write("5\n")
        f.write(f"@waitfile {sync2}\n")
        f.write("Line 0,0,0 10,10,0\n")
        f.write(f"@waitfile {sync2b}\n")
        # Document::FinalizePending() only runs when the *next*
        # Document::BeginChange() fires (see Document.cpp: BeginChange calls
        # FinalizePending() on itself first) - so the first Line's own edit
        # stays pending, and out of Document::ActivityLog(), until something
        # else begins another edit. This second, otherwise uninteresting
        # Line is exactly that: running it is what actually finalizes the
        # first Line into the Activity Log, the same way starting this
        # first Line above is what finalized Circle's entry (see check 9's
        # own comment).
        f.write("Line 5,5,0 6,6,0\n")
        f.write(f"@waitfile {sync3}\n")
        # NamedView Save is a plain, single-frame command (no options, no
        # pending-edit finalization dance like Circle/Line above), so one
        # more sync point around it is enough to observe Named Views go
        # empty -> populated, the same before/after pattern as checks
        # 3/6/9 above.
        f.write("NamedView Save MyView\n")
        f.write(f"@waitfile {sync4}\n")
        # Same shape as the NamedView Save check just above: NamedCPlane Save
        # is also a plain, single-frame command, so one more sync point is
        # enough to observe Named CPlanes go empty -> populated.
        f.write("NamedCPlane Save MyCPlane\n")
        f.write(f"@waitfile {sync5}\n")
        # Same shape again: SetCustomLinetype is also a plain, single-frame
        # command (Begin() applies it immediately once both Name= and
        # Pattern= are given - see cmd_annotate2.cpp's SetCustomLinetypeCommand),
        # so one more sync point is enough to observe Linetypes gain a new
        # entry (it starts non-empty, unlike Named Views/CPlanes, since the
        # document ships with built-in linetypes already).
        f.write("SetCustomLinetype Name=MyLinetype Pattern=5,2\n")
        f.write(f"@waitfile {sync6}\n")
        # Same shape again: ClippingPlaneCommand finishes in one line once it
        # has both corner points (the same way "Line 0,0,0 10,10,0" above
        # feeds Line's two points in one line), so one more sync point is
        # enough to observe Clipping Planes gain a new entry.
        f.write("ClippingPlane 0,0,0 5,5,0\n")
        f.write(f"@waitfile {sync7}\n")
        # Same shape again: LayoutCommand finishes in one line once a name is
        # given on the line itself (Begin's "!pos.empty()" branch - see
        # cmd_viewtools.cpp's LayoutCommand), so one more sync point is
        # enough to observe Layouts gain a new, active entry.
        f.write("Layout MyLayout\n")
        f.write(f"@waitfile {sync8}\n")
        # Block Manager's mirror needs an actual block definition to show,
        # and BlockCommand (see cmd_drafting.cpp) needs a real object
        # selection first - "SelAll" selects the Circle/Line objects the
        # earlier checks already created (pre-selection satisfies
        # BlockCommand's WantObjects immediately - see
        # CommandEngine::AfterCallback), then "Block" starts it, a base
        # point and a name finish it across two more script lines, the same
        # multi-line shape "Circle 0,0,0" / "5" used above for Command
        # Options.
        f.write("SelAll\n")
        f.write("Block\n")
        f.write("0,0,0\n")
        f.write("MyBlock\n")
        f.write(f"@waitfile {sync9}\n")
        # Same shape as NamedView Save above: LayerState Save is also a
        # plain, single-frame command (see cmd_drafting.cpp's
        # LayerStateCommand), so one more sync point is enough to observe
        # Layer State Manager gain a new entry.
        f.write("LayerState Save MyLayerState\n")
        f.write(f"@waitfile {sync10}\n")
        # Same shape again: SetDocumentUserText sets a key/value pair in one
        # line once both tokens are given (see cmd_state.cpp), so one more
        # sync point is enough to observe Document User Text gain a new
        # entry.
        f.write("SetDocumentUserText MyKey MyValue\n")
        f.write(f"@waitfile {sync11}\n")
        # Same shape again: PointLight finishes in one line once it has both
        # its options and its single point (Name= is consumed by
        # ConsumeOptionTokens before WantPoint even asks - see
        # cmd_render.cpp's own "PointLight 0,0,10 Intensity=2" comment - the
        # same mixed option/point-on-one-line shape "ClippingPlane 0,0,0
        # 5,5,0" already relies on above), so one more sync point is enough
        # to observe Lights gain a new entry.
        f.write("PointLight Name=MyLight 0,0,10\n")
        f.write(f"@waitfile {sync12}\n")
        # Same shape again: AnnotationStylesCommand is also a plain,
        # single-frame command (Immediate, see cmd_annotate2.cpp), so one
        # more sync point is enough to observe Annotation Styles gain a new,
        # current entry (it starts non-empty, unlike Named Views/CPlanes,
        # since the document ships with a "Default" annotation style
        # already - see check 12's own note for Linetypes).
        f.write("AnnotationStyles Name=MyStyle Height=2.5 Arrow=1 Font=Arial\n")
        f.write(f"@waitfile {sync13}\n")
        # Same shape again: EnvironmentsCommand is also a plain, single-frame
        # command (Immediate, see cmd_render.cpp), so one more sync point is
        # enough to observe the Environments accessible's "Background" row
        # change (it starts non-empty at the document's default "Sky"
        # background, unlike Named Views/CPlanes - see check 22's own note).
        f.write("Environments Background=Solid\n")
        f.write(f"@waitfile {sync14}\n")
        # MakeInvalidCurve (test/QC-only, see cmd_analyze.cpp) deliberately
        # builds one genuinely invalid curve, then Audit finds it - both
        # plain, single-frame Immediate commands, run on two consecutive
        # frames before this one sync point, the same way this script
        # already runs "Circle 0,0,0" then waits before feeding "5" - so one
        # sync point after both is enough to observe Audit Results gain a
        # new entry.
        f.write("MakeInvalidCurve\n")
        f.write("Audit\n")
        f.write(f"@waitfile {sync15}\n")
        # Same shape again: Undo is also a plain, single-frame command (see
        # Document::Undo, Document.cpp), so one more sync point is enough to
        # observe Redo History gain the entry Undo History just lost -
        # MakeInvalidCurve's own BeginChange left its label as the topmost
        # pending Undo History entry (see check 22/23's own note on
        # Environments/Audit Results not touching the object undo stack),
        # so this is what Undo hands to Redo History.
        f.write("Undo\n")
        f.write(f"@waitfile {sync_final}\n")

    procs = []
    dino8_proc = None
    try:
        # A private X server for the app's GLFW window - it still opens
        # one, invisibly, under --smoke - independent of the AT-SPI bus
        # below (AT-SPI2 has nothing to do with X11).
        xvfb = subprocess.Popen(["Xvfb", ":97", "-screen", "0", "1600x900x24"],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        procs.append(xvfb)
        time.sleep(1)

        # A private D-Bus session bus. --print-address on stdout (with
        # --nofork so the process, and thus its stdout pipe, stays alive)
        # avoids dbus-launch's shell-quoted multi-variable output, which
        # would need its own parsing.
        dbus_proc = subprocess.Popen(
            ["dbus-daemon", "--session", "--print-address", "--nofork"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        procs.append(dbus_proc)
        bus_address = dbus_proc.stdout.readline().strip()
        if not bus_address:
            die("dbus-daemon did not print a session bus address")

        env = dict(os.environ)
        env["DISPLAY"] = ":97"
        env["DBUS_SESSION_BUS_ADDRESS"] = bus_address
        env["XDG_CONFIG_HOME"] = os.path.join(tmp, "config")
        os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)

        registryd_proc = subprocess.Popen(
            [registryd, "--use-gnome-session=false"], env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        procs.append(registryd_proc)
        time.sleep(1)

        dino8_proc = subprocess.Popen(
            [binary, "--smoke", "5", "--script", script_path], env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        procs.append(dino8_proc)

        # pyatspi/libatspi discover the a11y bus (via org.a11y.Bus.GetAddress)
        # by reading DBUS_SESSION_BUS_ADDRESS from THIS process's own
        # environment the first time they're used - not just the child's.
        os.environ["DBUS_SESSION_BUS_ADDRESS"] = bus_address
        os.environ["DISPLAY"] = ":97"

        def find_app(timeout):
            deadline = time.time() + timeout
            while time.time() < deadline:
                try:
                    desktop = pyatspi.Registry.getDesktop(0)
                    for i in range(desktop.childCount):
                        app = desktop.getChildAtIndex(i)
                        if app is not None and app.name == "Dino8":
                            return app
                except Exception:
                    pass  # registration still in flight, or a transient D-Bus hiccup - just retry
                time.sleep(0.2)
            return None

        def find_child_by_name(parent, name, timeout):
            deadline = time.time() + timeout
            while time.time() < deadline:
                try:
                    for j in range(parent.childCount):
                        child = parent.getChildAtIndex(j)
                        if child is not None and child.name == name:
                            return child
                except Exception:
                    pass
                time.sleep(0.2)
            return None

        app = find_app(20)
        if app is None:
            if dino8_proc.poll() is None:
                dino8_proc.terminate()
            out, _ = dino8_proc.communicate(timeout=5)
            die(f"\"Dino8\" application accessible never appeared under the AT-SPI2 desktop within 20s. "
                f"Dino8 output so far:\n{out}")
        ok('"Dino8" application accessible is discoverable via the real AT-SPI2 desktop')

        cmdline = find_child_by_name(app, "Command Line", 10)
        if cmdline is None:
            die('"Command Line" accessible not found among the application\'s children')
        ok('"Command Line" accessible is discoverable via the real AT-SPI2 desktop')

        menu_bar = find_child_by_name(app, "Menu Bar", 10)
        if menu_bar is None:
            fail('"Menu Bar" accessible not found among the application\'s children')
        else:
            ok('"Menu Bar" accessible is discoverable via the real AT-SPI2 desktop')
            if menu_bar.getRoleName() != "menu bar":
                fail(f"Menu Bar's role is {menu_bar.getRoleName()!r}, expected \"menu bar\"")
            else:
                ok("Menu Bar's AT-SPI role is \"menu bar\"")
            file_menu = find_child_by_name(menu_bar, "File", 5)
            if file_menu is None:
                fail('"File" menu not found among Menu Bar\'s children')
            elif file_menu.getRoleName() != "menu":
                fail(f"File's role is {file_menu.getRoleName()!r}, expected \"menu\"")
            else:
                ok('"File" menu is discoverable under Menu Bar with AT-SPI role "menu"')

        layers = find_child_by_name(app, "Layers", 10)
        if layers is None:
            fail('"Layers" accessible not found among the application\'s children')
        else:
            ok('"Layers" accessible is discoverable via the real AT-SPI2 desktop')
            if layers.childCount < 1:
                fail("Layers has no ListItem children (expected at least the initial \"Default\" layer)")
            else:
                first_layer = layers.getChildAtIndex(0)
                if first_layer is None or "Default" not in first_layer.name:
                    fail(f"Layers' first child is not the Default layer (got {first_layer.name if first_layer else None!r})")
                else:
                    ok('Layers\' first ListItem names the initial "Default" layer')

        properties = find_child_by_name(app, "Properties", 10)
        if properties is None:
            die('"Properties" accessible not found among the application\'s children')
        ok('"Properties" accessible is discoverable via the real AT-SPI2 desktop')

        activity_log = find_child_by_name(app, "Activity Log", 10)
        if activity_log is None:
            fail('"Activity Log" accessible not found among the application\'s children')
        else:
            ok('"Activity Log" accessible is discoverable via the real AT-SPI2 desktop')

        named_views = find_child_by_name(app, "Named Views", 10)
        if named_views is None:
            fail('"Named Views" accessible not found among the application\'s children')
        else:
            ok('"Named Views" accessible is discoverable via the real AT-SPI2 desktop')
            if named_views.childCount != 0:
                fail(f"Named Views has {named_views.childCount} children before any view is saved (expected 0)")
            else:
                ok("Named Views has no ListItem children before any view is saved")

        named_cplanes = find_child_by_name(app, "Named CPlanes", 10)
        if named_cplanes is None:
            fail('"Named CPlanes" accessible not found among the application\'s children')
        else:
            ok('"Named CPlanes" accessible is discoverable via the real AT-SPI2 desktop')
            if named_cplanes.childCount != 0:
                fail(f"Named CPlanes has {named_cplanes.childCount} children before any cplane is saved (expected 0)")
            else:
                ok("Named CPlanes has no ListItem children before any cplane is saved")

        linetypes = find_child_by_name(app, "Linetypes", 10)
        if linetypes is None:
            fail('"Linetypes" accessible not found among the application\'s children')
        else:
            ok('"Linetypes" accessible is discoverable via the real AT-SPI2 desktop')
            if linetypes.childCount < 1:
                fail("Linetypes has no ListItem children (expected at least the built-in \"Continuous\" linetype)")
            else:
                first_linetype = linetypes.getChildAtIndex(0)
                if first_linetype is None or first_linetype.name != "Continuous":
                    fail(f"Linetypes' first child is not Continuous (got {first_linetype.name if first_linetype else None!r})")
                else:
                    ok('Linetypes\' first ListItem names the built-in "Continuous" linetype')

        materials = find_child_by_name(app, "Materials", 10)
        if materials is None:
            fail('"Materials" accessible not found among the application\'s children')
        else:
            ok('"Materials" accessible is discoverable via the real AT-SPI2 desktop')
            if materials.childCount != 0:
                fail(f"Materials has {materials.childCount} children in a fresh document (expected 0)")
            else:
                ok("Materials has no ListItem children in a fresh document (no built-in materials)")

        clipping_planes = find_child_by_name(app, "Clipping Planes", 10)
        if clipping_planes is None:
            fail('"Clipping Planes" accessible not found among the application\'s children')
        else:
            ok('"Clipping Planes" accessible is discoverable via the real AT-SPI2 desktop')
            if clipping_planes.childCount != 0:
                fail(f"Clipping Planes has {clipping_planes.childCount} children before any plane is created (expected 0)")
            else:
                ok("Clipping Planes has no ListItem children before any plane is created")

        layouts = find_child_by_name(app, "Layouts", 10)
        if layouts is None:
            fail('"Layouts" accessible not found among the application\'s children')
        else:
            ok('"Layouts" accessible is discoverable via the real AT-SPI2 desktop')
            if layouts.childCount != 0:
                fail(f"Layouts has {layouts.childCount} children before any layout is created (expected 0)")
            else:
                ok("Layouts has no ListItem children before any layout is created")

        block_manager = find_child_by_name(app, "Block Manager", 10)
        if block_manager is None:
            fail('"Block Manager" accessible not found among the application\'s children')
        else:
            ok('"Block Manager" accessible is discoverable via the real AT-SPI2 desktop')
            if block_manager.childCount != 0:
                fail(f"Block Manager has {block_manager.childCount} children before any block is defined (expected 0)")
            else:
                ok("Block Manager has no ListItem children before any block is defined")

        layer_state_manager = find_child_by_name(app, "Layer State Manager", 10)
        if layer_state_manager is None:
            fail('"Layer State Manager" accessible not found among the application\'s children')
        else:
            ok('"Layer State Manager" accessible is discoverable via the real AT-SPI2 desktop')
            if layer_state_manager.childCount != 0:
                fail(f"Layer State Manager has {layer_state_manager.childCount} children before any state is saved (expected 0)")
            else:
                ok("Layer State Manager has no ListItem children before any state is saved")

        document_user_text = find_child_by_name(app, "Document User Text", 10)
        if document_user_text is None:
            fail('"Document User Text" accessible not found among the application\'s children')
        else:
            ok('"Document User Text" accessible is discoverable via the real AT-SPI2 desktop')
            if document_user_text.childCount != 0:
                fail(f"Document User Text has {document_user_text.childCount} children before any key is set (expected 0)")
            else:
                ok("Document User Text has no ListItem children before any key is set")

        lights = find_child_by_name(app, "Lights", 10)
        if lights is None:
            fail('"Lights" accessible not found among the application\'s children')
        else:
            ok('"Lights" accessible is discoverable via the real AT-SPI2 desktop')
            if lights.childCount != 0:
                fail(f"Lights has {lights.childCount} children in a fresh document (expected 0)")
            else:
                ok("Lights has no ListItem children in a fresh document")

        annotation_styles = find_child_by_name(app, "Annotation Styles", 10)
        if annotation_styles is None:
            fail('"Annotation Styles" accessible not found among the application\'s children')
        else:
            ok('"Annotation Styles" accessible is discoverable via the real AT-SPI2 desktop')
            if annotation_styles.childCount < 1:
                fail("Annotation Styles has no ListItem children (expected at least the built-in \"Default\" style)")
            else:
                first_style = annotation_styles.getChildAtIndex(0)
                if first_style is None or first_style.name != "Default, current":
                    fail(f"Annotation Styles' first child is not the current \"Default\" style (got {first_style.name if first_style else None!r})")
                else:
                    ok('Annotation Styles\' first ListItem names the built-in "Default" style as current')

        document_notes = find_child_by_name(app, "Document Notes", 10)
        if document_notes is None:
            fail('"Document Notes" accessible not found among the application\'s children')
        else:
            ok('"Document Notes" accessible is discoverable via the real AT-SPI2 desktop')
            notes_text_iface = document_notes.queryText()
            notes_text = notes_text_iface.getText(0, -1) if notes_text_iface else None
            if notes_text != "":
                fail(f"Document Notes reports {notes_text!r} in a fresh document (expected the empty string)")
            else:
                ok("Document Notes reports the empty string in a fresh document")

        environments = find_child_by_name(app, "Environments", 10)
        if environments is None:
            fail('"Environments" accessible not found among the application\'s children')
        else:
            ok('"Environments" accessible is discoverable via the real AT-SPI2 desktop')
            if environments.childCount != 9:
                fail(f"Environments has {environments.childCount} children in a fresh document (expected 9 facts)")
            else:
                background_row = environments.getChildAtIndex(0)
                if background_row is None or background_row.name != "Background: Sky":
                    fail(f"Environments' first child is not the default \"Sky\" background (got {background_row.name if background_row else None!r})")
                else:
                    ok('Environments\' first ListItem names the document\'s default "Sky" background')

        audit_results = find_child_by_name(app, "Audit Results", 10)
        if audit_results is None:
            fail('"Audit Results" accessible not found among the application\'s children')
        else:
            ok('"Audit Results" accessible is discoverable via the real AT-SPI2 desktop')
            if audit_results.childCount != 0:
                fail(f"Audit Results has {audit_results.childCount} children before any Audit run (expected 0)")
            else:
                ok("Audit Results has no ListItem children before any Audit run")

        undo_history = find_child_by_name(app, "Undo History", 10)
        if undo_history is None:
            fail('"Undo History" accessible not found among the application\'s children')
        else:
            ok('"Undo History" accessible is discoverable via the real AT-SPI2 desktop')
            if undo_history.childCount != 0:
                fail(f"Undo History has {undo_history.childCount} children in a fresh document (expected 0)")
            else:
                ok("Undo History has no ListItem children in a fresh document")

        redo_history = find_child_by_name(app, "Redo History", 10)
        if redo_history is None:
            fail('"Redo History" accessible not found among the application\'s children')
        else:
            ok('"Redo History" accessible is discoverable via the real AT-SPI2 desktop')
            if redo_history.childCount != 0:
                fail(f"Redo History has {redo_history.childCount} children in a fresh document (expected 0)")
            else:
                ok("Redo History has no ListItem children in a fresh document")

        hatch_patterns = find_child_by_name(app, "Hatch Patterns", 10)
        if hatch_patterns is None:
            fail('"Hatch Patterns" accessible not found among the application\'s children')
        else:
            ok('"Hatch Patterns" accessible is discoverable via the real AT-SPI2 desktop')
            if hatch_patterns.childCount < 1:
                fail("Hatch Patterns has no ListItem children (expected at least the built-in \"SOLID\" pattern)")
            else:
                first_pattern = hatch_patterns.getChildAtIndex(0)
                if first_pattern is None or first_pattern.name != "SOLID":
                    fail(f"Hatch Patterns' first child is not SOLID (got {first_pattern.name if first_pattern else None!r})")
                else:
                    ok('Hatch Patterns\' first ListItem names the built-in "SOLID" pattern')

        plugins_node = find_child_by_name(app, "Plug-ins", 10)
        if plugins_node is None:
            fail('"Plug-ins" accessible not found among the application\'s children')
        else:
            ok('"Plug-ins" accessible is discoverable via the real AT-SPI2 desktop')
            # Non-empty by default: Application's own constructor scans the
            # binary's own exe_dir/plugins folder at startup, and this
            # repository's bundled example plug-ins land right there by
            # default build options (DINO8_BUILD_SAMPLE_PLUGIN/
            # DINO8_BUILD_EXAMPLE_PLUGINS, both ON) - so this only checks
            # each row is well-formed, not a specific count or order
            # (directory iteration order is filesystem-dependent).
            if plugins_node.childCount < 1:
                fail("Plug-ins has no ListItem children (expected at least the bundled example plug-ins)")
            else:
                bad = [plugins_node.getChildAtIndex(j).name for j in range(plugins_node.childCount)
                       if not (plugins_node.getChildAtIndex(j).name or "").endswith(("Loaded", "Error"))]
                if bad:
                    fail(f"Plug-ins has row(s) not ending in \"Loaded\"/\"Error\" (got {bad!r})")
                else:
                    names = [plugins_node.getChildAtIndex(j).name for j in range(plugins_node.childCount)]
                    ok(f"Plug-ins lists {plugins_node.childCount} already-loaded bundled plug-in(s), each naming "
                       f"its own Loaded/Error status ({names!r})")

        viewports = find_child_by_name(app, "Viewports", 10)
        if viewports is None:
            fail('"Viewports" accessible not found among the application\'s children')
        else:
            ok('"Viewports" accessible is discoverable via the real AT-SPI2 desktop')
            # A viewport row's name is "Perspective, active, display mode X"
            # (see BuildViewportsPanelNode), not the bare viewport name, so
            # find it with startswith rather than find_child_by_name's exact
            # match.
            perspective = None
            for j in range(viewports.childCount):
                child = viewports.getChildAtIndex(j)
                if child is not None and child.name.startswith("Perspective"):
                    perspective = child
                    break
            if perspective is None:
                fail(f"Viewports has no row for the default \"Perspective\" viewport (childCount={viewports.childCount})")
            elif "active" not in perspective.name:
                fail(f"Perspective viewport row does not report itself active (got {perspective.name!r})")
            else:
                ok(f"Viewports\' Perspective row reports itself active ({perspective.name!r})")

        cmd_options = find_child_by_name(app, "Command Options", 10)
        if cmd_options is None:
            die('"Command Options" accessible not found among the application\'s children')
        ok('"Command Options" accessible is discoverable via the real AT-SPI2 desktop')
        if cmd_options.childCount != 0:
            fail(f"Command Options has {cmd_options.childCount} children before any command runs (expected 0)")
        else:
            ok("Command Options has no ListItem children while no command is running")

        def option_names(node):
            names = []
            for j in range(node.childCount):
                child = node.getChildAtIndex(j)
                if child is not None:
                    names.append(child.name)
            return names

        open(sync0, "w").close()  # let the script run "Circle 0,0,0" (starts Circle, waits for a radius)

        deadline = time.time() + 10
        names = []
        while time.time() < deadline:
            names = option_names(cmd_options)
            if names:
                break
            time.sleep(0.2)
        if "Diameter" not in names:
            fail(f'Command Options does not list "Diameter" while Circle is running (got {names!r})')
        else:
            ok(f"Command Options lists Circle's option chips while it is running ({names!r})")

        open(sync1, "w").close()  # let the script run "5" (feeds the radius, finishing Circle)

        deadline = time.time() + 10
        remaining = option_names(cmd_options)
        while time.time() < deadline and remaining:
            remaining = option_names(cmd_options)
            time.sleep(0.2)
        if remaining:
            fail(f"Command Options still lists options after Circle finished within 10s (got {remaining!r})")
        else:
            ok("Command Options goes back to empty once the running command finishes")

        def find_objects_entry(properties_node):
            try:
                for j in range(properties_node.childCount):
                    child = properties_node.getChildAtIndex(j)
                    if child is not None and child.name.startswith("Objects:"):
                        return child.name
            except Exception:
                pass
            return None

        text_iface = cmdline.queryText()
        before = text_iface.getText(0, -1)
        if "Line 0,0,0 10,10,0" in before:
            fail("the Line command's text is already present before the command ran "
                 "(synchronization did not hold - the script ran ahead of this test)")
        else:
            ok("command-line text region's value is queryable before the command runs")

        properties_before = find_objects_entry(properties)
        if properties_before is None:
            fail('Properties has no "Objects: N" entry before the command ran')
        else:
            ok(f"Properties reports the document's object count before the command runs ({properties_before!r})")

        open(sync2, "w").close()  # let the script run "Line 0,0,0 10,10,0"

        deadline = time.time() + 10
        after = before
        while time.time() < deadline:
            try:
                after = text_iface.getText(0, -1)
            except Exception:
                pass
            if "Line 0,0,0 10,10,0" in after:
                break
            time.sleep(0.2)
        if "Line 0,0,0 10,10,0" not in after:
            fail(f"command-line text region's value did not update after the command ran within "
                 f"10s (last seen: {after!r})")
        else:
            ok("command-line text region's value updates after a command runs")

        deadline = time.time() + 10
        properties_after = properties_before
        while time.time() < deadline:
            properties_after = find_objects_entry(properties)
            if properties_after is not None and properties_after != properties_before:
                break
            time.sleep(0.2)
        if properties_before is not None and properties_after == properties_before:
            fail(f"Properties' object count did not update after the command ran within 10s "
                 f"(still {properties_after!r})")
        elif properties_after is not None:
            ok(f"Properties' object count updates after a command runs ({properties_before!r} -> {properties_after!r})")

        # This first Line's own BeginChange finalized Circle's edit (the
        # same fact the Activity Log comment just below relies on), so at
        # this exact point - after the first Line ran, before the second
        # one below - Undo History should read pending "Line" over
        # finalized "Circle", the same two-entry snapshot
        # DrawUndoMultipleWindow(app, /*redo=*/false) would show right now.
        if undo_history is not None:
            deadline = time.time() + 10
            names = []
            while time.time() < deadline:
                names = [undo_history.getChildAtIndex(j).name for j in range(undo_history.childCount)]
                if names:
                    break
                time.sleep(0.2)
            if names != ["1. Line", "2. Circle"]:
                fail(f"Undo History does not read [\"1. Line\", \"2. Circle\"] after the first Line command ran "
                     f"within 10s (got {names!r})")
            else:
                ok('Undo History reads ["1. Line", "2. Circle"] - pending Line over finalized Circle - '
                   "once the first Line command runs")

        # Line's own edit is not finalized into the Activity Log by its own
        # completion - Document::FinalizePending() only runs when the *next*
        # Document::BeginChange() fires (see Document.cpp) - it was starting
        # *this* first Line that finalized Circle's entry above, the same
        # way starting the second Line below is what will finalize this
        # first Line's own entry. So the right baseline for that is
        # whatever Activity Log holds right now (after Circle's entry has
        # already settled in, above), not some earlier snapshot - AT-SPI's
        # own cache can briefly lag behind the per-frame tree rebuild (see
        # the "GetItems ... /org/a11y/atspi/cache" warning this bridge
        # logs), but nothing here changes Activity Log's content again until
        # sync2b is opened, so a single read taken right before it is safe.
        activity_log_count_before = activity_log.childCount if activity_log is not None else None

        open(sync2b, "w").close()  # let the script run the second "Line 5,5,0 6,6,0"

        if activity_log is not None:
            deadline = time.time() + 10
            newest = None
            while time.time() < deadline:
                count = activity_log.childCount
                if activity_log_count_before is not None and count > activity_log_count_before:
                    newest = activity_log.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest is None:
                fail(f"Activity Log did not gain a new entry after a later edit finalized the first Line "
                     f"command within 10s (childCount stayed at {activity_log_count_before!r})")
            elif "Line" not in newest.name:
                fail(f"Activity Log's newest entry does not name the finalized Line command (got {newest.name!r})")
            else:
                ok(f"Activity Log gains a new entry naming a finalized command once a later edit flushes it "
                   f"({newest.name!r})")

        named_views_count_before = named_views.childCount if named_views is not None else None

        open(sync3, "w").close()  # let the script run "NamedView Save MyView"

        if named_views is not None:
            deadline = time.time() + 10
            newest_view = None
            while time.time() < deadline:
                count = named_views.childCount
                if named_views_count_before is not None and count > named_views_count_before:
                    newest_view = named_views.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_view is None:
                fail(f"Named Views did not gain a new entry after \"NamedView Save MyView\" ran within 10s "
                     f"(childCount stayed at {named_views_count_before!r})")
            elif newest_view.name != "MyView":
                fail(f"Named Views' newest entry does not name the saved view (got {newest_view.name!r})")
            else:
                ok(f"Named Views gains a new entry naming the saved view once \"NamedView Save\" runs "
                   f"({newest_view.name!r})")

        named_cplanes_count_before = named_cplanes.childCount if named_cplanes is not None else None

        open(sync4, "w").close()  # let the script run "NamedCPlane Save MyCPlane"

        if named_cplanes is not None:
            deadline = time.time() + 10
            newest_cplane = None
            while time.time() < deadline:
                count = named_cplanes.childCount
                if named_cplanes_count_before is not None and count > named_cplanes_count_before:
                    newest_cplane = named_cplanes.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_cplane is None:
                fail(f"Named CPlanes did not gain a new entry after \"NamedCPlane Save MyCPlane\" ran within 10s "
                     f"(childCount stayed at {named_cplanes_count_before!r})")
            elif newest_cplane.name != "MyCPlane":
                fail(f"Named CPlanes' newest entry does not name the saved cplane (got {newest_cplane.name!r})")
            else:
                ok(f"Named CPlanes gains a new entry naming the saved cplane once \"NamedCPlane Save\" runs "
                   f"({newest_cplane.name!r})")

        linetypes_count_before = linetypes.childCount if linetypes is not None else None

        open(sync5, "w").close()  # let the script run "SetCustomLinetype Name=MyLinetype Pattern=5,2"

        if linetypes is not None:
            deadline = time.time() + 10
            newest_linetype = None
            while time.time() < deadline:
                count = linetypes.childCount
                if linetypes_count_before is not None and count > linetypes_count_before:
                    newest_linetype = linetypes.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_linetype is None:
                fail(f"Linetypes did not gain a new entry after \"SetCustomLinetype Name=MyLinetype\" ran within "
                     f"10s (childCount stayed at {linetypes_count_before!r})")
            elif newest_linetype.name != "MyLinetype":
                fail(f"Linetypes' newest entry does not name the custom linetype (got {newest_linetype.name!r})")
            else:
                ok(f"Linetypes gains a new entry naming the custom linetype once \"SetCustomLinetype\" runs "
                   f"({newest_linetype.name!r})")

        clipping_planes_count_before = clipping_planes.childCount if clipping_planes is not None else None

        open(sync6, "w").close()  # let the script run "ClippingPlane 0,0,0 5,5,0"

        if clipping_planes is not None:
            deadline = time.time() + 10
            newest_plane = None
            while time.time() < deadline:
                count = clipping_planes.childCount
                if clipping_planes_count_before is not None and count > clipping_planes_count_before:
                    newest_plane = clipping_planes.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_plane is None:
                fail(f"Clipping Planes did not gain a new entry after \"ClippingPlane\" ran within 10s "
                     f"(childCount stayed at {clipping_planes_count_before!r})")
            elif ", on" not in newest_plane.name:
                fail(f"Clipping Planes' newest entry does not report itself on (got {newest_plane.name!r})")
            else:
                ok(f"Clipping Planes gains a new entry reporting itself on once \"ClippingPlane\" runs "
                   f"({newest_plane.name!r})")

        layouts_count_before = layouts.childCount if layouts is not None else None

        open(sync7, "w").close()  # let the script run "Layout MyLayout"

        if layouts is not None:
            deadline = time.time() + 10
            newest_layout = None
            while time.time() < deadline:
                count = layouts.childCount
                if layouts_count_before is not None and count > layouts_count_before:
                    newest_layout = layouts.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_layout is None:
                fail(f"Layouts did not gain a new entry after \"Layout MyLayout\" ran within 10s "
                     f"(childCount stayed at {layouts_count_before!r})")
            elif newest_layout.name != "MyLayout, active":
                fail(f"Layouts' newest entry does not name the new active layout (got {newest_layout.name!r})")
            else:
                ok(f"Layouts gains a new, active entry naming the new layout once \"Layout\" runs "
                   f"({newest_layout.name!r})")

        block_manager_count_before = block_manager.childCount if block_manager is not None else None

        open(sync8, "w").close()  # let the script run "SelAll" / "Block" / "0,0,0" / "MyBlock"

        if block_manager is not None:
            deadline = time.time() + 10
            newest_block = None
            while time.time() < deadline:
                count = block_manager.childCount
                if block_manager_count_before is not None and count > block_manager_count_before:
                    newest_block = block_manager.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_block is None:
                fail(f"Block Manager did not gain a new entry after \"Block\" ran within 10s "
                     f"(childCount stayed at {block_manager_count_before!r})")
            elif newest_block.name != "MyBlock":
                fail(f"Block Manager's newest entry does not name the new block (got {newest_block.name!r})")
            else:
                ok(f"Block Manager gains a new entry naming the new block, with its object/instance counts as its "
                   f"Description, once \"Block\" runs ({newest_block.name!r}, {newest_block.description!r})")

        layer_state_manager_count_before = layer_state_manager.childCount if layer_state_manager is not None else None

        open(sync9, "w").close()  # let the script run "LayerState Save MyLayerState"

        if layer_state_manager is not None:
            deadline = time.time() + 10
            newest_state = None
            while time.time() < deadline:
                count = layer_state_manager.childCount
                if layer_state_manager_count_before is not None and count > layer_state_manager_count_before:
                    newest_state = layer_state_manager.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_state is None:
                fail(f"Layer State Manager did not gain a new entry after \"LayerState Save MyLayerState\" ran "
                     f"within 10s (childCount stayed at {layer_state_manager_count_before!r})")
            elif newest_state.name != "MyLayerState":
                fail(f"Layer State Manager's newest entry does not name the saved state (got {newest_state.name!r})")
            else:
                ok(f"Layer State Manager gains a new entry naming the saved state once \"LayerState Save\" runs "
                   f"({newest_state.name!r})")

        document_user_text_count_before = document_user_text.childCount if document_user_text is not None else None

        open(sync10, "w").close()  # let the script run "SetDocumentUserText MyKey MyValue"

        if document_user_text is not None:
            deadline = time.time() + 10
            newest_entry = None
            while time.time() < deadline:
                count = document_user_text.childCount
                if document_user_text_count_before is not None and count > document_user_text_count_before:
                    newest_entry = document_user_text.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_entry is None:
                fail(f"Document User Text did not gain a new entry after \"SetDocumentUserText\" ran within 10s "
                     f"(childCount stayed at {document_user_text_count_before!r})")
            elif newest_entry.name != "MyKey" or newest_entry.description != "MyValue":
                fail(f"Document User Text's newest entry does not name/describe the new key/value "
                     f"(got {newest_entry.name!r} = {newest_entry.description!r})")
            else:
                ok(f"Document User Text gains a new entry naming the key with its value as the Description once "
                   f"\"SetDocumentUserText\" runs ({newest_entry.name!r} = {newest_entry.description!r})")

        lights_count_before = lights.childCount if lights is not None else None

        open(sync11, "w").close()  # let the script run "PointLight Name=MyLight 0,0,10"

        if lights is not None:
            deadline = time.time() + 10
            newest_light = None
            while time.time() < deadline:
                count = lights.childCount
                if lights_count_before is not None and count > lights_count_before:
                    newest_light = lights.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_light is None:
                fail(f"Lights did not gain a new entry after \"PointLight\" ran within 10s "
                     f"(childCount stayed at {lights_count_before!r})")
            elif newest_light.name != "MyLight (Point), on":
                fail(f"Lights' newest entry does not name/type/state the new light (got {newest_light.name!r})")
            else:
                ok(f"Lights gains a new entry naming, typing and stating the new light once \"PointLight\" runs "
                   f"({newest_light.name!r})")

        annotation_styles_count_before = annotation_styles.childCount if annotation_styles is not None else None

        open(sync12, "w").close()  # let the script run "AnnotationStyles Name=MyStyle Height=2.5 Arrow=1 Font=Arial"

        if annotation_styles is not None:
            deadline = time.time() + 10
            newest_style = None
            while time.time() < deadline:
                count = annotation_styles.childCount
                if annotation_styles_count_before is not None and count > annotation_styles_count_before:
                    newest_style = annotation_styles.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_style is None:
                fail(f"Annotation Styles did not gain a new entry after \"AnnotationStyles\" ran within 10s "
                     f"(childCount stayed at {annotation_styles_count_before!r})")
            elif newest_style.name != "MyStyle, current":
                fail(f"Annotation Styles' newest entry does not name the new, current style (got {newest_style.name!r})")
            elif newest_style.description != "Text height: 2.5; Arrow size: 1; Font: Arial":
                fail(f"Annotation Styles' newest entry does not describe its text height/arrow size/font "
                     f"(got {newest_style.description!r})")
            else:
                ok(f"Annotation Styles gains a new, current entry with its text height/arrow size/font as its "
                   f"Description once \"AnnotationStyles\" runs ({newest_style.name!r}, {newest_style.description!r})")

        open(sync13, "w").close()  # let the script run "Environments Background=Solid"

        if environments is not None:
            deadline = time.time() + 10
            background_row = None
            while time.time() < deadline:
                row = environments.getChildAtIndex(0)
                if row is not None and row.name == "Background: Solid":
                    background_row = row
                    break
                time.sleep(0.2)
            if background_row is None:
                fail("Environments' \"Background\" row did not change to \"Background: Solid\" after "
                     "\"Environments Background=Solid\" ran within 10s")
            else:
                ok('Environments\' "Background" row changes to "Background: Solid" once '
                   '"Environments Background=Solid" runs')

        audit_results_count_before = audit_results.childCount if audit_results is not None else None

        open(sync14, "w").close()  # let the script run "MakeInvalidCurve" then "Audit"

        if audit_results is not None:
            deadline = time.time() + 10
            newest_issue = None
            while time.time() < deadline:
                count = audit_results.childCount
                if audit_results_count_before is not None and count > audit_results_count_before:
                    newest_issue = audit_results.getChildAtIndex(count - 1)
                    break
                time.sleep(0.2)
            if newest_issue is None:
                fail(f"Audit Results did not gain a new entry after \"MakeInvalidCurve\"/\"Audit\" ran within 10s "
                     f"(childCount stayed at {audit_results_count_before!r})")
            elif not re.match(r"^Object \d+ \(curve\)$", newest_issue.name):
                fail(f"Audit Results' newest entry does not name an object id and \"Curve\" type "
                     f"(got {newest_issue.name!r})")
            elif not newest_issue.description:
                fail("Audit Results' newest entry carries no failure-reason Description")
            else:
                ok(f"Audit Results gains a new entry naming the invalid object's id and type, with its failure "
                   f"reason as its Description, once \"MakeInvalidCurve\"/\"Audit\" run "
                   f"({newest_issue.name!r}, {newest_issue.description!r})")

        undo_history_count_before = undo_history.childCount if undo_history is not None else None
        redo_history_count_before = redo_history.childCount if redo_history is not None else None

        open(sync15, "w").close()  # let the script run "Undo"

        if redo_history is not None:
            deadline = time.time() + 10
            newest_redo = None
            while time.time() < deadline:
                count = redo_history.childCount
                if redo_history_count_before is not None and count > redo_history_count_before:
                    newest_redo = redo_history.getChildAtIndex(0)
                    break
                time.sleep(0.2)
            if newest_redo is None:
                fail(f"Redo History did not gain a new entry after \"Undo\" ran within 10s "
                     f"(childCount stayed at {redo_history_count_before!r})")
            elif newest_redo.name != "1. MakeInvalidCurve":
                fail(f"Redo History's new entry does not name the undone MakeInvalidCurve edit (got {newest_redo.name!r})")
            else:
                ok('Redo History gains a "1. MakeInvalidCurve" entry once "Undo" runs')

        if undo_history is not None and undo_history_count_before is not None:
            deadline = time.time() + 10
            count = undo_history_count_before
            while time.time() < deadline:
                count = undo_history.childCount
                if count < undo_history_count_before:
                    break
                time.sleep(0.2)
            if count >= undo_history_count_before:
                fail(f"Undo History did not lose an entry after \"Undo\" ran within 10s "
                     f"(childCount stayed at {count!r})")
            else:
                ok(f"Undo History loses its topmost entry once \"Undo\" runs "
                   f"({undo_history_count_before!r} -> {count!r})")

        open(sync_final, "w").close()  # let the app finish its remaining frames/script and exit

        try:
            out, _ = dino8_proc.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            dino8_proc.kill()
            out, _ = dino8_proc.communicate()
            fail("Dino8 did not exit within 20s after the accessibility checks completed")
            out = out or ""

        if dino8_proc.returncode not in (0, None):
            fail(f"Dino8 exited with code {dino8_proc.returncode}:\n{out}")
        elif "smoke:" not in out:
            fail(f"Dino8 exited 0 but printed no \"smoke:\" line:\n{out}")
        else:
            ok("Dino8 exited cleanly (code 0) after the accessibility session")

    finally:
        for p in procs:
            if p.poll() is None:
                p.terminate()
        for p in procs:
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"{FAILURES} failure(s)")
    sys.exit(1 if FAILURES else 0)


if __name__ == "__main__":
    main()
