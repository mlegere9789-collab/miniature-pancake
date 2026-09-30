# The Dino 8 compute server

`Dino8 --serve PORT` starts a minimal HTTP server: POST a Lua script to it,
get back its printed output. This is the first real step toward a
Rhino.Compute-style "geometry as a service" story for Dino 8 - before this
there was no server/socket/HTTP code anywhere in the source at all.

**It is not Rhino.Compute.** There is no authentication, no concurrency (one
request is serviced at a time), no REST resource model, and no geometry
(de)serialization format - a script gets and returns text, the same way the
command line does. See "What this deliberately does not do" below for the
honest scope.

## Running it

```
Dino8 --serve 8080
```

```
$ curl -s -X POST --data 'rs.Command("Box 0,0,0 5,5,0 5")
print("objects: " .. #rs.AllObjects())' http://127.0.0.1:8080/run
history: objects: 1
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
- Every request must be a `POST`; anything else gets back `405 Method Not
  Allowed`.
- The request body is run as a Lua script against the *same running
  document* every other script/command-line interaction shares - state
  (objects, layers, the undo stack, ...) persists from one request to the
  next, exactly like typing into the command line yourself would.
- The response body is the script's captured `print()` output, one line per
  `history: ...` entry, the same lines that would otherwise land in the
  Command History panel. HTTP status is `200` on success or `500` if the
  script raised a Lua error or tried to suspend on an interactive prompt
  (see below).

## What this deliberately does not do

- **No interactive prompts.** A script that calls `rs.GetPoint`/
  `rs.GetObject`/etc. tries to suspend and wait for a pick a synchronous
  HTTP request has no way to supply; the server detects this, cancels the
  script (the same way pressing Escape on an interactive `RunScript` would),
  and returns a `500` explaining why instead of hanging the connection.
- **No concurrency.** One connection is accepted and fully serviced (read
  request, run script, write response, close) before the next is even
  accepted - see `net/ComputeServer.h`'s `PollOnce`. A slow or malicious
  client gets a bounded read timeout and a maximum request size instead of
  being able to hang the server, but there is no queueing or parallel
  execution of multiple scripts.
- **No auth, no TLS.** This binds `127.0.0.1` and is meant for local
  automation (a build script, a CI job, a local tool talking to a running
  Dino 8), not for exposing to a network you don't trust.
- **No geometry wire format.** Unlike Rhino.Compute's JSON-in/JSON-out
  geometry payloads, everything in and out of this server is Lua source
  text and printed strings - getting actual geometry out means having the
  script `print()` whatever representation you need (coordinates, a JSON
  string you build yourself with `dino8`'s existing query functions, ...).

Closing any of the above further is real, separate follow-up work, not
something this server already does quietly.
