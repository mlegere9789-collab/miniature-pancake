# The Dino 8 compute server

`Dino8 --serve PORT` starts a minimal HTTP server: POST a Lua or Python
script to it, get back its printed output. This is the first real step
toward a Rhino.Compute-style "geometry as a service" story for Dino 8 -
before this there was no server/socket/HTTP code anywhere in the source at
all.

**It is not Rhino.Compute.** There is no concurrency (one request is
serviced at a time), no REST resource model, and no geometry (de)serialization
format - a script gets and returns text, the same way the command line does.
Authentication is real but minimal - an optional shared bearer token, not
OAuth/API keys/per-user accounts. See "What this deliberately does not do"
below for the honest scope.

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
- `--serve-token TOKEN` requires every request to carry a matching
  `Authorization: Bearer TOKEN` header; a request with no such header, or
  the wrong one, gets back `401 Unauthorized` before the body is ever run as
  a script. Omit the flag to keep the server fully open, as before.
- Every request must be a `POST`; anything else gets back `405 Method Not
  Allowed`.
- `POST /run` runs the request body as a **Lua** script; `POST /run/python`
  runs it as a **Python** script against the embedded `dino8` module (only
  on a build with Python support - see `BATCH_SCRIPTING.md`/`PythonEngine.h`;
  a build without it answers `500` with an explicit "no DINO8_HAVE_PYTHON"
  message instead of silently doing nothing). Any other path gets back `404
  Not Found`.
- Either way, the script runs against the *same running document* every
  other script/command-line interaction shares - state (objects, layers, the
  undo stack, ...) persists from one request to the next, exactly like
  typing into the command line yourself would, and Lua and Python requests
  interleave freely against that one shared document.
- The response body is the script's captured `print()` output, one line per
  call, the same text that would otherwise land in the Command History panel
  (there prefixed with `history: `; the compute server's response is the
  raw text only). HTTP status is `200` on success or `500` if the script
  raised an error or (Lua only) tried to suspend on an interactive prompt
  (see below).

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
- **No TLS, and auth is a single shared secret, not real accounts.**
  `--serve-token` is a plain-text bearer token checked with `==` (no
  constant-time comparison, no hashing, no per-caller tokens, no rotation)
  over a plaintext loopback-only socket - good enough to stop an unrelated
  local process or script from hitting an unprotected endpoint by accident,
  nowhere near OAuth/API-key-management. This binds `127.0.0.1` and is meant
  for local automation (a build script, a CI job, a local tool talking to a
  running Dino 8), not for exposing to a network you don't trust even with
  a token set.
- **No geometry wire format.** Unlike Rhino.Compute's JSON-in/JSON-out
  geometry payloads, everything in and out of this server is script source
  text and printed strings - getting actual geometry out means having the
  script `print()` whatever representation you need (coordinates, a JSON
  string you build yourself with `dino8`'s/`rs`'s existing query functions,
  ...).

Closing any of the above further is real, separate follow-up work, not
something this server already does quietly.
