# Dino 8 Touch

A touch-first companion for Dino 8 (the Rhino-for-iPad equivalent), shipped as an
installable Progressive Web App. It talks to a running desktop Dino 8 session over
your network, or opens a model on the device by itself. Plain HTML/CSS/JS: no CDN,
no external fonts, no trackers, no build step.

Files: `web/` is the app (served by the desktop at `/touch/`), `tests/` holds the
automated test, the server half lives in `src/net/TouchApi.cpp` (routes) and
`src/net/TouchGate.cpp` (auth, blocklist, static files).

## Start the desktop side

```
Dino8 --serve 8080 --serve-touch
```

Prints, for example:

```
serve: listening on port 8080
touch: open http://127.0.0.1:8080/touch/#token=3f9c...e1 on the tablet (same network)
```

* `--serve-touch` turns on the PWA (`/touch/`) and its API (`/touch/api/`) and, if you
  did not pass `--serve-token`/`--serve-token-file`, generates a one-session random
  pairing token. With your own `--serve-token NAME:TOKEN` (or a token file) that
  token is the pairing token instead.
* By default the server listens on `127.0.0.1` only. To reach it from a tablet add
  `--serve-lan`: it then binds all interfaces but still drops any peer that is not a
  loopback, private (10/8, 172.16/12, 192.168/16) or link-local address, and it
  refuses to start without a token.
* `--touch-dir DIR` serves the PWA files from `DIR` instead of the bundled
  `data/touch` copy (useful when developing the app).

## Pair and install

1. Open `http://<desktop-ip>:8080/touch/#token=...` on the tablet (type the line the
   desktop printed; the token is in the URL fragment, which browsers never send to
   a server, and the app removes it from the address bar). Or open `/touch/` and fill
   in the connect panel: desktop address `host:port` and the pairing token.
2. Install: iOS Safari Share > Add to Home Screen; Android Chrome menu > Install app.
   The app then starts full screen and, thanks to the service worker, even with no
   network.

**Important, secure-context limit.** Browsers only run service workers (and offer
install) on `https://` pages or on `localhost`. The desktop server speaks plain HTTP,
so over a LAN IP address the page works (WebGL, touch, API) but the offline cache and
the home-screen *install* are only available from `localhost`/an HTTPS origin. TLS for
the desktop server is the first item deferred below.

## Gestures

| Gesture | Action |
|---|---|
| One finger drag | Orbit (0.008 rad per pixel) |
| Two-finger drag | Pan |
| Pinch | Zoom |
| Double tap | Zoom extents |
| Tap (Select tool) | Select the object under the finger; tap empty space to clear |
| Long press (0.5 s) | Select / toggle the object under the finger, in any tool |
| Three-finger tap | Undo |

## Toolbar (all targets at least 44x44 CSS px)

Select, Move, Rotate, Scale, Measure | Layers, Display, Command | Undo, Redo, Extents.

* **Move / Rotate / Scale** act on the selection with a live client-side preview;
  on release the app sends the equivalent command lines (`Move`, base point, target
  point; `Rotate`, centre, angle; `Scale`, origin, factor) to the desktop.
* **Measure** takes two taps (on the model or the ground plane) and shows the
  distance and the dx/dy/dz components.
* **Layers** lists the desktop's layers and shows/hides them.
* **Display** switches Shaded, Wireframe, Ghosted and X-Ray (client-side rendering).
* **Command** is a command line: type any Dino 8 command, see its output.

## Standalone

*Open model* reads `.obj` and `.stl` (ASCII or binary) from the device; *Load demo*
opens a cube. Standalone mode can orbit, pan, zoom, select, measure, switch display
modes and apply Move/Rotate/Scale to the local copy. It cannot save or run commands.

## Server API (all under `/touch/api/`, `Authorization: Bearer <token>`)

`GET ping`, `GET state`, `GET scene` (tessellated triangles/edges/curves/points per
visible object), `POST command` (`{"lines":[...]}` or plain text; returns the new
history lines, the object count and any pending prompt), `POST select`
(`{"ids":[..],"mode":"set|add|toggle|clear"}`), `POST layer`
(`{"index":N,"visible":bool}`). Static app files under `/touch/` need no token (they
are code, not data). Without a configured token the API answers 403; with a wrong or
missing token, 401. Script-running commands (`RunScript`, `RunPythonScript`, `= <lua>`,
`Python`, `Macro`, plug-in installs) are refused over this channel. Everything else
is the normal command line, so a paired device can `Open`/`Save`/`Export` paths on the
desktop: treat the pairing token like a password.

## Test

Two levels, both run in CI (the Linux job of `.github/workflows/dino8-app.yml`):

* `dino8_test_touch_api` (ctest, `tests/test_touch_api.cpp`): the Application-free
  half of the server side (`src/net/TouchGate.cpp`) without a window or a browser.
  Bearer-token auth (missing, wrong, prefix, wrong scheme refused; right and
  `sha256:` file entries accepted), the 401 / 403 answers, the `--serve-lan`
  token requirement, the script/plug-in command blocklist and the `/touch/`
  static-file path whitelist (`..`, encoded, backslash, absolute-path, hidden
  files, other extensions and non-GET methods refused).
* `node tests/touch_gestures.test.js /path/to/Dino8`: the PWA in headless Chromium
  against a real headless session. Needs node, the pinned `playwright` package
  (`npm ci` in this directory installs it from `package-lock.json`),
  `npx playwright install --with-deps chromium`, and `xvfb-run`. `tests/smoke.sh`
  runs it (`TCH_` lines). Locally it prints a skip line when a prerequisite is
  missing; CI sets `DINO8_REQUIRE_TOUCH=1`, which turns that skip into a FAIL.

## Deferred (round 2 and beyond)

* TLS (HTTPS) on the desktop server, so the service worker/installation also work
  over a LAN address; QR code for the pairing URL; token rotation UI.
* Full command palette with the catalog, options and prompts as touch widgets, and
  object snaps / point input by tapping on the model.
* Stylus sketching (Apple Pencil / S Pen pressure, palm rejection) and a sketch-to-curve
  command.
* AR placement (WebXR) of the model at scale.
* Sync and collaboration: pushing edits made offline back, live updates instead of the
  2.5 s polling, and the existing collab relay.
* Gumball-style gizmo with axis handles and numeric entry; box/crossing window select;
  object properties; saving the standalone model; more mesh formats (3dm, glTF).
* Real iOS/Android device verification (only emulated touch in headless Chromium so far).
