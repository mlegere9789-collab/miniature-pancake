// A minimal, real TCP relay for PARITY_MAP.md's "Real-time multi-user
// collaboration / co-editing" item (app_ecosystem) / "Real-time multi-user
// collaborative editing" (app_ux) - the same evidence closes both, per that
// document's own cross-reference. Before this there was no network code
// anywhere that let two running instances of the app share a document's
// edits with each other.
//
// Same honestly-scoped shape as net/ComputeServer.h: one small, real
// building block, not a competing product (no encryption, no NAT
// traversal). BSD/POSIX sockets on Linux/macOS and Winsock2 on Windows,
// behind the one small shim in net/Sockets.h (handle type, close call,
// error source, non-blocking switch and the one-time WSAStartup are the
// only things that differ; the wire behaviour is identical on every
// platform, and tests/test_collab_auth.cpp plus the COL_ smoke section
// are the same spec on each). Start()/Connect() still fail with a clear
// message, never silently pretending to listen or connect, when the
// socket layer itself refuses (WSAStartup, socket, bind, listen or
// connect failing).
//
// Access control: with a token set (SetToken) every
// connection must pass an HMAC-SHA256 challenge/response before a single
// frame is relayed to or from it - the token itself never crosses the
// wire, only HMAC(token, per-connection nonce). That is AUTHENTICATION,
// not confidentiality: the document snapshots that follow travel in clear
// text, and a party on the path could read or inject after the handshake.
// Start() binds loopback unless `lan` is set; the session layer only allows
// that together with a token.
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
#include <functional>
#include <string>
#include <utility>
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

// Handshake frames (token mode only). Server -> client: "DINO8COLLAB1 CHALLENGE <32 hex nonce>". Client -> server: "DINO8COLLAB1 AUTH
// <64 hex HMAC-SHA256(token, nonce)>". Server -> client: "DINO8COLLAB1 OK" or "DINO8COLLAB1 DENIED" (then the connection is closed).
constexpr const char* kCollabHandshakeTag = "DINO8COLLAB1 ";

class CollabServer {
 public:
  CollabServer() = default;
  ~CollabServer();
  CollabServer(const CollabServer&) = delete;
  CollabServer& operator=(const CollabServer&) = delete;

  // Binds and listens on 127.0.0.1:`port` (0 asks the OS for a free
  // ephemeral port - see Port() below). Returns false, with `error` set to
  // a human-readable reason, if the socket/bind/listen calls fail (or Winsock
  // cannot be initialised).
  // `lan` binds every interface (0.0.0.0) instead of loopback only.
  bool Start(int port, std::string& error, bool lan = false);
  void Stop();
  // A non-empty token makes every connection authenticate (see the top of this file) before it is relayed to or from. Set it before
  // Start(); an empty token (the default) is the old open relay.
  void SetToken(const std::string& token) { token_ = token; }
  bool AuthRequired() const { return !token_.empty(); }
  // A connection that has not authenticated within this long is dropped (default 10 s).
  void SetAuthTimeoutMs(int ms) { auth_timeout_ms_ = ms; }
  bool Lan() const { return lan_; }
  // "127.0.0.1" or "0.0.0.0" while listening.
  std::string BoundAddress() const { return listen_fd_ < 0 ? std::string() : (lan_ ? "0.0.0.0" : "127.0.0.1"); }
  // Connections that completed the handshake (every connection, when no token is set) / that were rejected: wrong or missing token,
  // a frame sent before authenticating, or a bad handshake.
  size_t AuthedCount() const;
  size_t RejectedCount() const { return rejected_; }
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
  // error). A frame is never cut short for a slow reader: what the OS will
  // not take right now waits in that peer's own queue and is sent as the
  // peer drains it (a peer that falls more than a few frames behind is
  // dropped instead - see kMaxQueuedBytes in Collab.cpp).
  int PollOnce(int timeout_ms = 0);

 private:
  struct ClientConn {
    std::intptr_t fd = -1;  // a POSIX fd or a Winsock SOCKET (see net/Sockets.h); -1 = none
    std::string inbuf;
    std::string outbuf;     // bytes accepted for this peer that the OS has not taken yet
    size_t out_off = 0;     // outbuf[0, out_off) is already sent
    bool authed = true;
    std::string nonce;
    long long accepted_ms = 0;
  };
  // Appends `wire` to `c`'s queue and sends what the OS takes right now. False: the connection broke or the peer fell too far behind.
  static bool QueueWire(ClientConn& c, const std::string& wire);
  static bool FlushOut(ClientConn& c);
  std::intptr_t listen_fd_ = -1;
  int port_ = 0;
  std::vector<ClientConn> clients_;
  std::string token_;
  bool lan_ = false;
  int auth_timeout_ms_ = 10000;
  size_t rejected_ = 0;
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
  // Called repeatedly while Send() waits for the OS to take a large frame. A host whose own client talks to its own relay in the same
  // thread sets it to the relay's PollOnce(0), so the relay keeps draining the very socket Send() is filling (otherwise a frame bigger
  // than the loopback buffers could never finish). Unset (the default): Send() just waits.
  void SetWaitCallback(std::function<void()> callback) { wait_callback_ = std::move(callback); }

  // With a token set the client answers the host's challenge (inside PollOnce); Send() then holds payloads back until the host has
  // accepted it, so nothing is ever sent unauthenticated. PollOnce returns false, with the reason, when the host rejects the token,
  // requires one this client lacks, or (token set) never asks for it within the timeout (default 5 s).
  void SetToken(const std::string& token) { token_ = token; }
  // True once the host accepted the token - or right away when no token is set.
  bool IsAuthenticated() const { return token_.empty() || authed_; }
  void SetAuthTimeoutMs(int ms) { auth_timeout_ms_ = ms; }

  bool Send(const std::string& payload, std::string& error);

  // Reads whatever is currently available (non-blocking) and appends every
  // complete frame extracted to `out`. Returns false only on a real
  // connection error/close (and disconnects); an empty `out` with nothing
  // currently available is not an error.
  bool PollOnce(std::vector<std::string>* out, std::string& error);

 private:
  // Writes all of `wire`, waiting while the OS buffer is full (and calling wait_callback_); false with `error` set on a real failure or
  // when the peer has not taken a single byte for kSendStallMs (Collab.cpp).
  bool WriteAll(const std::string& wire, std::string& error);
  std::intptr_t fd_ = -1;  // a POSIX fd or a Winsock SOCKET (see net/Sockets.h); -1 = none
  std::function<void()> wait_callback_;
  std::string inbuf_;
  std::string token_;
  bool authed_ = false;
  bool answered_ = false;
  long long connected_ms_ = 0;
  int auth_timeout_ms_ = 5000;
  std::vector<std::string> held_;
};

}  // namespace dino8::app
