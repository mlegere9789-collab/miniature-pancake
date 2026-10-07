# The Dino 8 compute server

`Dino8 --serve PORT` starts a minimal HTTP server: POST a Lua or Python
script to it, get back its printed output. This is the first real step
toward a Rhino.Compute-style "geometry as a service" story for Dino 8 -
before this there was no server/socket/HTTP code anywhere in the source at
all.

**It is not Rhino.Compute.** There is no concurrency (one request is
serviced at a time) and no REST resource model. `GET /objects` (below) is a
real, if minimal, structured JSON listing of what's in the document, and
`?geometry=1` adds each object's own geometry for the four kinds simple
enough to serialize honestly right now (point, mesh, a NURBS curve's own
degree/control points/weights/knots, and a single untrimmed NURBS
surface's own degree/control grid/weights/knots in each of its two
directions); `POST /objects` is the other direction - send one of those
same four shapes back in and it's added to the document - so geometry can
now get *into* this server as structured data too, not just out. Every
other kind (polysurface, SubD, point cloud) still only comes back as
whatever a script's own `print()`
output happens to contain (plain text by default; `/run`/`/run/python`
also answer with structured JSON instead of plain text when asked - see
below). Authentication is real but minimal - one or more bearer tokens,
each optionally naming its own caller, not OAuth/API keys/real accounts.
See "What this deliberately does not do" below for the honest scope.

## Running it

```
Dino8 --serve 8080
```

```
$ curl -s -X POST --data 'rs.Command("Box 0,0,0 5,5,0 5")
print("objects: " .. #rs.AllObjects())' http://127.0.0.1:8080/run
objects: 1
```

Like `--script` (see `BATCH_SCRIPTING.md`), `--serve` runs headless: the
window is created hidden, and the process still needs a real or virtual
display for its GL context (Xvfb+llvmpipe on a Linux server/CI, exactly the
same as every other headless mode this app already has).

- `--serve 0` asks the OS for a free ephemeral port instead of a fixed one;
  the port actually bound is printed on startup (`serve: listening on port
  N`).
- `--serve-max-requests N` exits the process after N requests have been
  serviced, instead of running until killed - used by `tests/smoke.sh` for
  a deterministic, self-terminating run; end users normally omit it.
- `--serve-token [NAME:]TOKEN` requires every request to carry a matching
  `Authorization: Bearer TOKEN` header; a request with no such header, or
  one matching none of the registered tokens, gets back `401 Unauthorized`
  before the body is ever run as a script. Omit the flag entirely to keep
  the server fully open, as before. Repeatable - give it more than once to
  register more than one acceptable token, so distinct callers (a CI job, a
  local tool, a teammate's script) can each hold their own instead of
  sharing one secret; a `NAME:` prefix names that token's caller (e.g.
  `--serve-token ci:abc123 --serve-token alice:def456`), echoed back as
  `"caller"` in a JSON response (`Accept: application/json` - see below) so
  a caller can confirm which of its own credentials authenticated the
  request. A bare `TOKEN` with no prefix works exactly as it always did
  (anonymous - no `"caller"` field in the JSON response).
- `POST /run` runs the request body as a **Lua** script; `POST /run/python`
  runs it as a **Python** script against the embedded `dino8` module (only
  on a build with Python support - see `BATCH_SCRIPTING.md`/`PythonEngine.h`;
  a build without it answers `500` with an explicit "no DINO8_HAVE_PYTHON"
  message instead of silently doing nothing). `GET /objects` (below) is the
  one other supported route; anything else gets back `404 Not Found`, and
  the wrong HTTP method on a route that exists (`GET /run`, `POST /objects`,
  ...) gets back `405 Method Not Allowed`.
- Either way, the script runs against the *same running document* every
  other script/command-line interaction shares - state (objects, layers, the
  undo stack, ...) persists from one request to the next, exactly like
  typing into the command line yourself would, and Lua and Python requests
  interleave freely against that one shared document.
- The response body is the script's captured `print()` output, one line per
  call, the same text that would otherwise land in the Command History panel
  (there prefixed with `history: `; the compute server's response is the
  raw text only) - unless the request carries `Accept: application/json`, in
  which case the same output comes back as `{"ok": true|false, "output":
  [...], "error": "...", "caller": "..."}` (`error` only present when the
  script was rejected for suspending on an interactive prompt - see below;
  `caller` only present when `--serve-token` named the token that
  authenticated this request) instead of plain text. HTTP status is `200`
  on success or `500` if the script raised an error or tried to suspend on
  an interactive prompt (see below) - the same either way, JSON or not.
- `GET /objects` returns every object currently in the document as a JSON
  array - `id`, `type` (the same name `What`/`ObjectType` print, e.g.
  `"polysurface"`), `name`, `layer` (its full path), and `bbox` (`{"min":
  [x,y,z], "max": [x,y,z]}`, or `null` for an object `BoundingBoxOf` can't
  box). A real, minimal structured wire format for *what exists and roughly
  where*.
- `GET /objects?geometry=1` additionally carries each object's own
  `geometry` field: `{"point": [x,y,z]}` for a point; `{"vertices":
  [[x,y,z], ...], "faces": [[i,i,i], ...]}` for a mesh (0-based indices,
  a 4-entry face for a quad); `{"degree": d, "rational": bool,
  "control_points": [[x,y,z], ...], "weights": [...], "knots": [...]}`
  for a curve; and `{"degree_u": du, "degree_v": dv, "u_count": uc,
  "v_count": vc, "rational": bool, "control_points": [[x,y,z], ...]
  (uc*vc entries, flattened u*v_count+v), "weights": [...], "knots_u":
  [...], "knots_v": [...]}` for a single untrimmed surface - the exact
  NURBS definition OpenNURBS itself stores in both the curve and surface
  cases, enough to reconstruct the object exactly, not sampled points;
  `weights` is only present when `rational` is `true`. Every other kind
  (polysurface, SubD, point cloud) still gets `"geometry": null` - a
  Brep's multiple trimmed faces (each with its own trimming curves) and a
  SubD's control-cage topology are a substantially larger undertaking
  than one untrimmed surface's single control grid, not attempted here.
- `POST /objects` is the write side: send a JSON body shaped like one of
  the four `"geometry"` payloads above, tagged with a `"type"` field
  (`"point"`, `"mesh"`, `"curve"` or `"surface"`), and it's added to the
  document - `{"ok": true, "id": N}` on success (the new object's id), or
  `{"ok": false, "error": "..."}` with `400 Bad Request` for a malformed
  body, an unrecognized `"type"`, or a shape that doesn't parse (wrong
  array lengths, a `"knots"` count that doesn't match
  `degree + control_points - 1`, ...). A `"curve"` needs `"degree"` and
  `"control_points"`; a `"surface"` needs `"degree_u"`/`"degree_v"` and
  `"u_count"`/`"v_count"` (each >= its own degree + 1) alongside
  `"control_points"` (exactly `u_count * v_count` entries); either kind's
  `"knots"`/`"knots_u"`/`"knots_v"`/`"weights"` are optional (when
  omitted, the curve/surface gets a real, if generic, clamped-uniform
  knot vector instead of the exact one `GET`'s own payload for an
  existing object would show). Still genuinely limited to the same four
  kinds `GET`'s own `?geometry=1` already serializes - there is no way to
  POST a polysurface, SubD or point cloud in as structured data, and a
  caller still has to build one of those by `print()`ing a script that
  constructs it the normal way.

```
$ curl -s -X POST --data 'import dino8
id = dino8.doc.Objects.AddBox(dino8.Point3d(0,0,0), dino8.Vector3d(5,5,5))
print("volume: %.1f" % dino8.doc.Objects.SurfaceVolume(id))' http://127.0.0.1:8080/run/python
volume: 125.0

$ curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8080/run   # no token given
401
$ curl -s -o /dev/null -w '%{http_code}\n' -H 'Authorization: Bearer hunter2' --data 'print(1)' http://127.0.0.1:8080/run
200
```
(the second pair assumes the server was started with `--serve-token hunter2`.)

```
$ curl -s -H 'Accept: application/json' -H 'Authorization: Bearer abc123' --data 'print(1)' http://127.0.0.1:8080/run
{"ok":true,"output":["1"],"caller":"ci"}
```
(assumes `--serve-token ci:abc123` - a named token's caller comes back in the JSON response so it can confirm which credential authenticated it; a bare, unnamed token like `hunter2` above never adds a `"caller"` field.)

```
$ curl -s -H 'Accept: application/json' --data 'print("hi")' http://127.0.0.1:8080/run
{"ok":true,"output":["hi"]}
$ curl -s http://127.0.0.1:8080/objects
[{"id":1,"type":"polysurface","name":"","layer":"Default","bbox":{"min":[0.000000,0.000000,0.000000],"max":[5.000000,5.000000,5.000000]}}]
```

```
$ curl -s --data 'rs.AddPoint(1,2,3)
rs.AddLine({0,0,0},{10,0,0})' http://127.0.0.1:8080/run >/dev/null
$ curl -s http://127.0.0.1:8080/objects?geometry=1
[{"id":1,"type":"polysurface", ..., "geometry":null},
 {"id":2,"type":"point", ..., "geometry":{"point":[1.000000,2.000000,3.000000]}},
 {"id":3,"type":"curve", ..., "geometry":{"degree":1,"rational":false,"control_points":[[0.000000,0.000000,0.000000],[10.000000,0.000000,0.000000]],"knots":[0.000000,1.000000]}}]
```
(the box's `"geometry"` is `null` - polysurfaces aren't one of the four kinds `?geometry=1` serializes yet.)

```
$ curl -s --data '{"type":"mesh","vertices":[[0,0,0],[1,0,0],[0,1,0]],"faces":[[0,1,2]]}' http://127.0.0.1:8080/objects
{"ok":true,"id":4}
$ curl -s --data '{"type":"bogus"}' http://127.0.0.1:8080/objects
{"ok":false,"error":"unknown or missing \"type\" (expected \"point\", \"mesh\" or \"curve\")"}
```
(the mesh just POSTed shows up at id 4 in a follow-up `GET /objects?geometry=1`, with the exact vertices/faces sent.)

## What this deliberately does not do

- **No interactive prompts, over either language.** A script that calls
  `rs.GetPoint`/`rs.GetObject`/etc. (Lua) or `dino8.GetPoint()`/`GetString()`/
  `GetReal()`/`GetInteger()`/`GetObject()`/`GetObjects()` (Python - all six
  are a real suspend/resume now, see `script/PythonEngine.h`) tries to
  suspend and wait for a pick a synchronous HTTP request has no way to
  supply; the server detects this (`app.Lua().Suspended()` /
  `app.Python().Suspended()` right after `Start()` returns) and cancels the
  script (the same way pressing Escape on an interactive
  `RunScript`/`RunPythonScript` would), returning a `500` explaining why
  instead of hanging the connection open forever. Python has no remaining
  unported prompt - every `rs.Get*`-equivalent now suspends the same way.
- **No concurrency.** One connection is accepted and fully serviced (read
  request, run script, write response, close) before the next is even
  accepted - see `net/ComputeServer.h`'s `PollOnce`. A slow or malicious
  client gets a bounded read timeout and a maximum request size instead of
  being able to hang the server, but there is no queueing or parallel
  execution of multiple scripts.
- **No TLS, and auth is a handful of plain-text secrets, not real accounts.**
  Each `--serve-token` is a plain-text bearer token, checked with `main.cpp`'s
  `ConstantTimeEquals` (a real constant-time comparison against every
  registered candidate, so an unrelated local process can't learn one byte
  at a time by timing repeated guesses) over a plaintext loopback-only
  socket - distinct callers can now each hold their own named token rather
  than sharing exactly one secret (see above), but there is still no
  hashing, no rotation, no expiry, and no revocation short of restarting
  the server with a different set of `--serve-token` flags. Good enough to
  stop an unrelated local process or script from hitting an unprotected
  endpoint by accident, nowhere near OAuth/API-key-management. This binds
  `127.0.0.1` and is meant for local automation (a build script, a CI job,
  a teammate's script talking to a running Dino 8), not for exposing to a
  network you don't trust even with tokens set.
- **No geometry (de)serialization format for most geometry.** `GET
  /objects?geometry=1`/`POST /objects` (above) now carry real data both
  ways for points, meshes, curves and surfaces (a curve's own exact
  degree/control points/weights/knots, and a single untrimmed surface's
  own degree/control grid/weights/knots in each of its two directions -
  not sampled points either way) - genuine structured data, not printed
  text - but polysurfaces, SubDs and point clouds all still come back
  `"geometry": null` from `GET` and can't be `POST`ed at all: a Brep's
  multiple trimmed faces (each with its own trimming curves) and a SubD's
  control-cage topology are each a substantially larger design than one
  untrimmed surface's single control grid, and neither exists yet.
  Getting their actual shape data out, or building one from scratch,
  still means having a script `print()`/construct whatever representation
  you need (coordinates, a JSON string you build yourself with `dino8`'s/
  `rs`'s existing query functions, ...).

Closing any of the above further is real, separate follow-up work, not
something this server already does quietly.
