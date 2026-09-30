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

        open(sync8, "w").close()  # let the app finish its remaining frames/script and exit

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
