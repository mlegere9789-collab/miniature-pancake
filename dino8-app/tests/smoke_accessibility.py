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
    sync1 = os.path.join(tmp, "sync1")
    sync2 = os.path.join(tmp, "sync2")
    script_path = os.path.join(tmp, "script.txt")
    with open(script_path, "w") as f:
        # `@waitfile` (like the built-in `@wait N` frames directive) needs
        # its `@` prefix to be recognized as a script directive at all - a
        # bare `waitfile ...` line is otherwise just fed straight to
        # CommandEngine::Execute() as an (unknown) command and the script
        # keeps going immediately, which is exactly the race this
        # synchronization exists to avoid.
        f.write(f"@waitfile {sync1}\n")
        f.write("Line 0,0,0 10,10,0\n")
        f.write(f"@waitfile {sync2}\n")

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

        def find_command_line(timeout):
            deadline = time.time() + timeout
            while time.time() < deadline:
                try:
                    desktop = pyatspi.Registry.getDesktop(0)
                    for i in range(desktop.childCount):
                        app = desktop.getChildAtIndex(i)
                        if app is None or app.name != "Dino8":
                            continue
                        for j in range(app.childCount):
                            child = app.getChildAtIndex(j)
                            if child is not None and child.name == "Command Line":
                                return child
                except Exception:
                    pass  # registration still in flight, or a transient D-Bus hiccup - just retry
                time.sleep(0.2)
            return None

        cmdline = find_command_line(20)
        if cmdline is None:
            if dino8_proc.poll() is None:
                dino8_proc.terminate()
            out, _ = dino8_proc.communicate(timeout=5)
            die("\"Command Line\" accessible never appeared under the AT-SPI2 desktop within 20s. "
                f"Dino8 output so far:\n{out}")
        ok('"Command Line" accessible is discoverable via the real AT-SPI2 desktop')

        text_iface = cmdline.queryText()
        before = text_iface.getText(0, -1)
        if "Line 0,0,0 10,10,0" in before:
            fail("the Line command's text is already present before the command ran "
                 "(synchronization did not hold - the script ran ahead of this test)")
        else:
            ok("command-line text region's value is queryable before the command runs")

        open(sync1, "w").close()  # let the script run "Line 0,0,0 10,10,0"

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

        open(sync2, "w").close()  # let the app finish its remaining frames/script and exit

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
