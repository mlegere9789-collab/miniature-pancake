// A minimal, real TCP relay for PARITY_MAP.md's "Real-time multi-user
// collaboration / co-editing" item (app_ecosystem) / "Real-time multi-user
// collaborative editing" (app_ux) - the same evidence closes both, per that
// document's own cross-reference. Before this there was no network code
// anywhere that let two running instances of the app share a document's
// edits with each other.
//
// Same honestly-scoped, POSIX-sockets-only shape as net/ComputeServer.h:
// one small, real building block, not a competing product (no encryption,
// no auth, no NAT traversal - loopback/LAN use, same as that file). Start()
// always fails with a clear message on a platform without POSIX sockets
// (Windows) rather than silently pretending to listen, exactly like
// ComputeServer::Start / AccessibilityLinux's AT-SPI2 bridge.
//
// CollabServer is a pure byte relay (a "hub"): it never looks inside a
// frame, it just forwards whatever one connected client sends to every
// OTHER currently connected client - the same star topology a simple
// chat/game relay uses, and the simplest topology that actually converges
// without a CRDT/operational-transform engine. CollabSession.h is the layer
// that actually knows what the bytes mean (a serialized .3dm snapshot of
// the whole document) - this file stays ignorant of that, the same
// separation ComputeServer.h (HTTP framing) keeps from main.cpp (what a
// request actually *does*).
//
// Framing: a 4-byte big-endian length prefix followed by that many payload
// bytes - the smallest real framing that lets an arbitrarily large payload
// (a whole .3dm file) be told apart from the next one on the same stream,
// the same problem HTTP's Content-Length solves for ComputeServer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dino8::app {

// A frame whose declared length would exceed this is rejected outright
// (TryParseFrame returns false forever for that stream, not just "not
// enough yet"; PollOnce/CollabClient::PollOnce close the offending
// connection) - the same finite-cap-on-an-untrusted-length-field discipline
// as ComputeServer.h's kMaxRequestBytes, sized generously enough for a real
// multi-megabyte .3dm snapshot.
constexpr std::uint32_t kMaxFrameBytes = 64u * 1024u * 1024u;

// Looks for one complete frame at the start of `buffer`. Returns true and
// fills `payload`/`consumed` (the number of bytes of `buffer` the frame
// occupied, prefix included) once one is fully available. Returns false if
// `buffer` doesn't yet hold a complete frame - including while only part of
// the 4-byte length prefix itself has arrived - so the caller should read
// more bytes and try again. Pure, no I/O: exercised directly with
// in-memory byte strings in tests/test_collab.cpp, the same "prove the
// parsing logic without opening a socket" split ComputeServer.h's
// TryParseHttpRequest uses.
bool TryParseFrame(const std::string& buffer, std::string* payload, size_t* consumed);

// Prepends `payload`'s length as a 4-byte big-endian prefix. The inverse of
// TryParseFrame.
std::string BuildFrame(const std::string& payload);

class CollabServer {
 public:
  CollabServer() = default;
  ~CollabServer();
  CollabServer(const CollabServer&) = delete;
  CollabServer& operator=(const CollabServer&) = delete;

  // Binds and listens on 127.0.0.1:`port` (0 asks the OS for a free
  // ephemeral port - see Port() below). Returns false, with `error` set to
  // a human-readable reason, if the socket/bind/listen calls fail or this
  // build has no POSIX socket support.
  bool Start(int port, std::string& error);
  void Stop();
  bool IsRunning() const { return listen_fd_ >= 0; }
  int Port() const { return port_; }
  size_t ClientCount() const { return clients_.size(); }

  // Accepts any one pending new connection and services every currently
  // connected client that has data ready to read, relaying every complete
  // frame received from one client to every OTHER currently connected
  // client (never back to its own sender - the thing that makes this a
  // relay and not an echo). Waits up to `timeout_ms` for the first bit of
  // activity on any watched fd (0 = don't block at all, the right choice
  // for calling this once per frame/iteration of a real-time loop, same as
  // ComputeServer::PollOnce). A client that disconnects, or that sends a
  // frame declaring a length over kMaxFrameBytes, is dropped. Returns the
  // number of frames relayed this call (0 if nothing was ready - not an
  // error).
  int PollOnce(int timeout_ms = 0);

 private:
  struct ClientConn {
    int fd = -1;
    std::string inbuf;
  };
  int listen_fd_ = -1;
  int port_ = 0;
  std::vector<ClientConn> clients_;
};

class CollabClient {
 public:
  CollabClient() = default;
  ~CollabClient();
  CollabClient(const CollabClient&) = delete;
  CollabClient& operator=(const CollabClient&) = delete;

  // Connects to a CollabServer already listening at host:port. host must be
  // a numeric IPv4 address (127.0.0.1 for CollabSession's own loopback host
  // connection) - no DNS resolution, keeping this exactly as small as
  // ComputeServer's own IPv4-loopback-only scope.
  bool Connect(const std::string& host, int port, std::string& error);
  void Disconnect();
  bool IsConnected() const { return fd_ >= 0; }

  bool Send(const std::string& payload, std::string& error);

  // Reads whatever is currently available (non-blocking) and appends every
  // complete frame extracted to `out`. Returns false only on a real
  // connection error/close (and disconnects); an empty `out` with nothing
  // currently available is not an error.
  bool PollOnce(std::vector<std::string>* out, std::string& error);

 private:
  int fd_ = -1;
  std::string inbuf_;
};

}  // namespace dino8::app
