// A minimal synchronous TCP/HTTP request server - the smallest real step
// toward PARITY_MAP.md's "Cloud/network compute service (Rhino.Compute
// equivalent)" item, which before this had no server/socket/HTTP code
// anywhere in the source at all.
//
// This is nowhere near Rhino.Compute itself (no auth, no concurrency, no
// geometry (de)serialization format, no REST resource model) - it is one
// request at a time, one script per request: POST a Lua script as the
// request body, get back its captured print() output as the response body.
// main.cpp's `--serve PORT` wires this to the same LuaEngine every
// interactive command line and RunScript already use, so a script posted
// over the network can build/query geometry in the running document exactly
// like a local script can. See docs/COMPUTE_SERVER.md for the wire format.
//
// POSIX sockets only, same honestly-scoped-by-platform shape as
// AccessibilityLinux.cpp's AT-SPI2 bridge: Start() always fails with a
// clear message on a platform without POSIX sockets (Windows) rather than
// silently pretending to listen.
#pragma once

#include <functional>
#include <string>

namespace dino8::app {

struct HttpRequest {
  std::string method;
  std::string path;
  std::string body;
};

struct HttpResponse {
  int status = 200;
  std::string content_type = "text/plain";
  std::string body;
};

// Looks for one complete HTTP/1.1 request (a request line, headers ending
// in a blank line, then a Content-Length body if one was declared) at the
// start of `buffer`. Pure parsing, no I/O: ComputeServer::PollOnce feeds it
// real socket bytes as they arrive; the unit test in
// tests/test_compute_server.cpp feeds it in-memory byte strings directly,
// including a request split across multiple partial buffers, so the
// parsing logic itself is fully covered without opening a single socket.
//
// Returns true and fills `out`/`consumed` (the number of bytes of `buffer`
// the request occupied) once a full request is available. Returns false if
// `buffer` doesn't yet hold a complete request (the caller should read more
// bytes and try again) - this never happens for a malformed request line or
// header block missing its blank-line terminator forever, since
// ComputeServer::PollOnce bounds how many bytes it will read before giving
// up and responding 400.
bool TryParseHttpRequest(const std::string& buffer, HttpRequest* out, size_t* consumed);

// Formats a minimal, valid HTTP/1.1 response: status line, Content-Type,
// Content-Length, "Connection: close" (this server never keeps a
// connection open past one request/response), a blank line, then the body.
std::string BuildHttpResponse(const HttpResponse& resp);

using ComputeHandler = std::function<HttpResponse(const HttpRequest&)>;

class ComputeServer {
 public:
  ComputeServer() = default;
  ~ComputeServer();
  ComputeServer(const ComputeServer&) = delete;
  ComputeServer& operator=(const ComputeServer&) = delete;

  // Binds and listens on 127.0.0.1:`port` (0 asks the OS for a free
  // ephemeral port - see Port() below). Returns false, with `error` set to
  // a human-readable reason, if the socket/bind/listen calls fail (e.g. the
  // port is already in use) or this build has no POSIX socket support.
  bool Start(int port, std::string& error);
  void Stop();
  bool IsRunning() const { return listen_fd_ >= 0; }
  // The port actually bound (useful after Start(0)); 0 if not running.
  int Port() const { return port_; }

  // Services at most one connection: waits up to `timeout_ms` for one to
  // arrive (0 = don't block at all - the right choice for calling this
  // once per iteration of main.cpp's existing per-frame loop, same as
  // every other --smoke/--script/--stress per-frame hook there), reads its
  // full request, calls `handler`, writes the response and closes the
  // connection. A request that never completes (bad client, or one that
  // exceeds the internal 8 MiB request-size cap) gets a 400/413 response
  // instead of hanging this call forever. Returns true if a connection was
  // accepted and serviced (regardless of the response status), false if no
  // connection was waiting.
  bool PollOnce(const ComputeHandler& handler, int timeout_ms = 0);

 private:
  int listen_fd_ = -1;
  int port_ = 0;
};

}  // namespace dino8::app
