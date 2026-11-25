#include "net/Collab.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <random>

#include "net/Sockets.h"
#include "util/Hmac.h"

namespace dino8::app {

namespace {

void EncodeBe32(std::uint32_t v, char out[4]) {
  out[0] = static_cast<char>((v >> 24) & 0xFF);
  out[1] = static_cast<char>((v >> 16) & 0xFF);
  out[2] = static_cast<char>((v >> 8) & 0xFF);
  out[3] = static_cast<char>(v & 0xFF);
}

std::uint32_t DecodeBe32(const char* p) {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(p[0])) << 24) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 8) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(p[3]));
}

long long NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string RandomNonceHex() {
  static const char* digits = "0123456789abcdef";
  std::random_device rd;
  std::string out;
  for (int i = 0; i < 16; ++i) {
    const unsigned v = rd() & 0xFF;
    out.push_back(digits[v >> 4]);
    out.push_back(digits[v & 15]);
  }
  return out;
}

bool StartsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

// One relay peer may have at most this much queued (sent by the relay, not yet taken by the peer's OS buffer) before it is dropped: two
// maximal frames. A peer that reads this slowly is not keeping up with the session anyway, and the bound keeps one stalled peer from
// growing the relay's memory without limit.
constexpr size_t kMaxQueuedBytes = 2u * (static_cast<size_t>(kMaxFrameBytes) + 4u);

// A client whose send() makes no progress at all for this long gives up (the host is not reading), rather than freezing its caller forever.
constexpr long long kSendStallMs = 30000;

// How many peers one select() can watch (the listening socket takes one slot): FD_SETSIZE is 1024 on POSIX and 64 on Winsock.
constexpr size_t kMaxPeers = static_cast<size_t>(FD_SETSIZE) - 1;

}  // namespace

bool TryParseFrame(const std::string& buffer, std::string* payload, size_t* consumed) {
  if (buffer.size() < 4) return false;
  const std::uint32_t len = DecodeBe32(buffer.data());
  if (len > kMaxFrameBytes) return false;  // caller closes the connection; never buffered forever
  if (buffer.size() < 4u + len) return false;
  *payload = buffer.substr(4, len);
  *consumed = 4u + len;
  return true;
}

std::string BuildFrame(const std::string& payload) {
  char prefix[4];
  EncodeBe32(static_cast<std::uint32_t>(payload.size()), prefix);
  std::string out(prefix, 4);
  out += payload;
  return out;
}

// ---- CollabServer ----------------------------------------------------

CollabServer::~CollabServer() { Stop(); }

bool CollabServer::Start(int port, std::string& error, bool lan) {
  Stop();
  if (!sock::EnsureWinsock(error, "collab server")) return false;
  const sock::Native fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock::IsInvalid(fd)) {
    error = "collab server: socket() failed: " + sock::LastError();
    return false;
  }
#ifndef _WIN32
  // Lets a restarted host rebind its port while the previous instance's connections are still in TIME_WAIT. Deliberately NOT set on
  // Winsock, where SO_REUSEADDR means something else entirely: it lets a second socket bind the very same address while the first is
  // still actively listening (port hijacking), with no TIME_WAIT benefit (Windows allows that rebind anyway).
  const int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(lan ? INADDR_ANY : INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    error = "collab server: bind() failed: " + sock::LastError();
    sock::Close(fd);
    return false;
  }
  if (::listen(fd, 16) != 0) {
    error = "collab server: listen() failed: " + sock::LastError();
    sock::Close(fd);
    return false;
  }
  sock::SetNonBlocking(fd);
  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  port_ = (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) ? ntohs(bound.sin_port) : port;
  listen_fd_ = sock::FromNative(fd);
  lan_ = lan;
  rejected_ = 0;
  return true;
}

void CollabServer::Stop() {
  for (ClientConn& c : clients_) if (c.fd >= 0) sock::Close(sock::ToNative(c.fd));
  if (listen_fd_ >= 0) sock::Close(sock::ToNative(listen_fd_));
  clients_.clear();
  listen_fd_ = -1;
  port_ = 0;
  lan_ = false;
}

size_t CollabServer::AuthedCount() const {
  size_t n = 0;
  for (const ClientConn& c : clients_) if (c.fd >= 0 && c.authed) ++n;
  return n;
}

bool CollabServer::FlushOut(ClientConn& c) {
  const sock::Native s = sock::ToNative(c.fd);
  while (c.out_off < c.outbuf.size()) {
    const size_t slice = std::min(c.outbuf.size() - c.out_off, sock::kMaxSendSlice);
    const sock::IoResult n = ::send(s, c.outbuf.data() + c.out_off, static_cast<sock::IoLength>(slice), 0);
    if (n > 0) { c.out_off += static_cast<size_t>(n); continue; }
    if (n < 0 && sock::WouldBlock()) break;  // the peer's buffer is full: the rest goes out as it drains (see PollOnce's write set)
    return false;                            // a real error: the peer is gone
  }
  if (c.out_off >= c.outbuf.size()) {
    c.outbuf.clear();
    c.out_off = 0;
  } else if (c.out_off > (size_t{1} << 20) && c.out_off > c.outbuf.size() / 2) {
    c.outbuf.erase(0, c.out_off);
    c.out_off = 0;
  }
  return true;
}

bool CollabServer::QueueWire(ClientConn& c, const std::string& wire) {
  if (c.outbuf.size() - c.out_off + wire.size() > kMaxQueuedBytes) return false;
  c.outbuf += wire;
  return FlushOut(c);
}

int CollabServer::PollOnce(int timeout_ms) {
  if (listen_fd_ < 0) return 0;
  const sock::Native listen_fd = sock::ToNative(listen_fd_);

  fd_set read_fds, write_fds;
  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);
  FD_SET(listen_fd, &read_fds);
  sock::Native max_fd = listen_fd;
  bool want_write = false;
  for (const ClientConn& c : clients_) {
    if (c.fd < 0) continue;
    const sock::Native s = sock::ToNative(c.fd);
    FD_SET(s, &read_fds);
    if (c.out_off < c.outbuf.size()) { FD_SET(s, &write_fds); want_write = true; }  // a slow reader's queue drains as soon as it can
    max_fd = std::max(max_fd, s);
  }
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  const int ready = ::select(sock::SelectNfds(max_fd), &read_fds, want_write ? &write_fds : nullptr, nullptr, &tv);
  if (ready <= 0) {
    // nothing to read, but idle unauthenticated connections still expire
    const long long t = NowMs();
    bool any = false;
    for (ClientConn& c : clients_) {
      if (c.fd >= 0 && !c.authed && t - c.accepted_ms > auth_timeout_ms_) { sock::Close(sock::ToNative(c.fd)); c.fd = -1; ++rejected_; any = true; }
    }
    if (any) clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const ClientConn& c) { return c.fd < 0; }), clients_.end());
    return 0;
  }

  // Accept at most one new connection per call - plenty for a per-frame
  // poll loop (another will be picked up next call), and keeps this
  // symmetric with ComputeServer::PollOnce's own "service one thing" shape.
  if (FD_ISSET(listen_fd, &read_fds)) {
    const sock::Native conn_fd = ::accept(listen_fd, nullptr, nullptr);
    if (!sock::IsInvalid(conn_fd)) {
      if (clients_.size() >= kMaxPeers) {
        sock::Close(conn_fd);  // one select() cannot watch more sockets than an fd_set holds
      } else {
        sock::SetNonBlocking(conn_fd);
        sock::SetNoDelay(conn_fd);
        ClientConn conn;
        conn.fd = sock::FromNative(conn_fd);
        conn.accepted_ms = NowMs();
        if (!token_.empty()) {
          conn.authed = false;
          conn.nonce = RandomNonceHex();
          QueueWire(conn, BuildFrame(std::string(kCollabHandshakeTag) + "CHALLENGE " + conn.nonce));
        }
        clients_.push_back(std::move(conn));
      }
    }
  }

  // Hand more queued bytes to every peer whose buffer has room again.
  std::vector<bool> drop(clients_.size(), false);
  for (size_t i = 0; i < clients_.size(); ++i) {
    ClientConn& c = clients_[i];
    if (c.fd < 0 || c.out_off >= c.outbuf.size() || !FD_ISSET(sock::ToNative(c.fd), &write_fds)) continue;
    if (!FlushOut(c)) drop[i] = true;
  }

  // Read whatever is ready from every existing client.
  char chunk[65536];
  for (size_t i = 0; i < clients_.size(); ++i) {
    ClientConn& c = clients_[i];
    if (c.fd < 0 || !FD_ISSET(sock::ToNative(c.fd), &read_fds)) continue;
    while (true) {
      const sock::IoResult n = ::recv(sock::ToNative(c.fd), chunk, static_cast<sock::IoLength>(sizeof(chunk)), 0);
      if (n > 0) {
        c.inbuf.append(chunk, static_cast<size_t>(n));
        if (c.inbuf.size() > static_cast<size_t>(kMaxFrameBytes) + 4) { drop[i] = true; break; }
        continue;
      }
      if (n == 0) { drop[i] = true; }                      // peer closed
      else if (!sock::WouldBlock()) { drop[i] = true; }    // a real error (a reset connection): Winsock keeps reporting it, so it must not be retried forever
      break;  // n < 0 and would-block: nothing more right now
    }
  }

  // Extract every complete frame from every client's buffer and relay it to
  // every OTHER still-live client - the actual relay step.
  int relayed = 0;
  for (size_t i = 0; i < clients_.size(); ++i) {
    ClientConn& sender = clients_[i];
    if (sender.fd < 0) continue;
    std::string payload;
    size_t consumed = 0;
    while (!drop[i] && TryParseFrame(sender.inbuf, &payload, &consumed)) {
      sender.inbuf.erase(0, consumed);
      if (!sender.authed) {
        // The only frame an unauthenticated connection may send is the answer to its challenge; anything else, or a wrong
        // answer, rejects it. Nothing it sends is ever relayed.
        const std::string want = std::string(kCollabHandshakeTag) + "AUTH " + util::HmacSha256Hex(token_, sender.nonce);
        if (util::ConstantTimeEqual(payload, want)) {
          sender.authed = true;
          if (!QueueWire(sender, BuildFrame(std::string(kCollabHandshakeTag) + "OK"))) drop[i] = true;
        } else {
          QueueWire(sender, BuildFrame(std::string(kCollabHandshakeTag) + "DENIED"));
          drop[i] = true;
          ++rejected_;
        }
        continue;
      }
      const std::string wire = BuildFrame(payload);
      for (size_t j = 0; j < clients_.size(); ++j) {
        if (j == i || clients_[j].fd < 0 || drop[j] || !clients_[j].authed) continue;
        // A slow peer never blocks the others and never gets half a frame: the rest of this frame waits in its own queue.
        if (!QueueWire(clients_[j], wire)) drop[j] = true;
      }
      ++relayed;
    }
    // A buffer that still can't yield a frame despite exceeding the cap
    // means a bad/oversized length prefix - drop that connection.
    if (sender.inbuf.size() > static_cast<size_t>(kMaxFrameBytes) + 4) drop[i] = true;
  }
  // A connection that never answers its challenge does not get to sit on a slot (and its buffering) forever.
  const long long now = NowMs();
  for (size_t i = 0; i < clients_.size(); ++i)
    if (clients_[i].fd >= 0 && !clients_[i].authed && !drop[i] && now - clients_[i].accepted_ms > auth_timeout_ms_) { drop[i] = true; ++rejected_; }

  for (size_t i = 0; i < clients_.size(); ++i) {
    if (drop[i] && clients_[i].fd >= 0) { sock::Close(sock::ToNative(clients_[i].fd)); clients_[i].fd = -1; }
  }
  clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const ClientConn& c) { return c.fd < 0; }),
                  clients_.end());
  return relayed;
}

// ---- CollabClient ------------------------------------------------------

CollabClient::~CollabClient() { Disconnect(); }

bool CollabClient::Connect(const std::string& host, int port, std::string& error) {
  Disconnect();
  if (!sock::EnsureWinsock(error, "collab client")) return false;
  const sock::Native fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock::IsInvalid(fd)) {
    error = "collab client: socket() failed: " + sock::LastError();
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    error = "collab client: '" + host + "' is not a numeric IPv4 address";
    sock::Close(fd);
    return false;
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    error = "collab client: connect() failed: " + sock::LastError();
    sock::Close(fd);
    return false;
  }
  sock::SetNonBlocking(fd);
  sock::SetNoDelay(fd);
  fd_ = sock::FromNative(fd);
  inbuf_.clear();
  authed_ = false;
  answered_ = false;
  held_.clear();
  connected_ms_ = NowMs();
  return true;
}

void CollabClient::Disconnect() {
  if (fd_ >= 0) sock::Close(sock::ToNative(fd_));
  fd_ = -1;
  inbuf_.clear();
  held_.clear();
}

bool CollabClient::WriteAll(const std::string& wire, std::string& error) {
  const sock::Native s = sock::ToNative(fd_);
  size_t sent = 0;
  long long stalled_since = 0;  // 0 = not stalled right now
  while (sent < wire.size()) {
    const size_t slice = std::min(wire.size() - sent, sock::kMaxSendSlice);
    const sock::IoResult n = ::send(s, wire.data() + sent, static_cast<sock::IoLength>(slice), 0);
    if (n > 0) { sent += static_cast<size_t>(n); stalled_since = 0; continue; }
    if (n < 0 && sock::WouldBlock()) {  // the OS buffer is full: let the relay (if it is in this process) drain it, then wait for room
      const long long t = NowMs();
      if (stalled_since == 0) stalled_since = t;
      else if (t - stalled_since > kSendStallMs) { error = "collab client: send() made no progress (the host is not reading)"; return false; }
      if (wait_callback_) wait_callback_();
      sock::WaitWritable(s, 2);
      continue;
    }
    error = "collab client: send() failed: " + sock::LastError();
    return false;
  }
  return true;
}

bool CollabClient::Send(const std::string& payload, std::string& error) {
  if (fd_ < 0) {
    error = "collab client: not connected";
    return false;
  }
  if (!IsAuthenticated()) { held_.push_back(payload); return true; }  // sent once the host accepts the token
  return WriteAll(BuildFrame(payload), error);
}

bool CollabClient::PollOnce(std::vector<std::string>* out, std::string& error) {
  if (fd_ < 0) {
    error = "collab client: not connected";
    return false;
  }
  bool peer_closed = false;
  char chunk[65536];
  while (true) {
    const sock::IoResult n = ::recv(sock::ToNative(fd_), chunk, static_cast<sock::IoLength>(sizeof(chunk)), 0);
    if (n > 0) {
      inbuf_.append(chunk, static_cast<size_t>(n));
      if (inbuf_.size() > static_cast<size_t>(kMaxFrameBytes) + 4) {
        error = "collab client: peer sent an oversized frame";
        Disconnect();
        return false;
      }
      continue;
    }
    if (n == 0) { peer_closed = true; break; }  // frames already buffered (a DENIED answer) are still read below
    if (sock::WouldBlock()) break;              // nothing more right now - not an error
    error = "collab client: recv() failed: " + sock::LastError();
    Disconnect();
    return false;
  }

  std::string payload;
  size_t consumed = 0;
  while (TryParseFrame(inbuf_, &payload, &consumed)) {
    inbuf_.erase(0, consumed);
    if (StartsWith(payload, kCollabHandshakeTag)) {
      const std::string what = payload.substr(std::strlen(kCollabHandshakeTag));
      if (StartsWith(what, "CHALLENGE ")) {
        if (token_.empty()) {
          error = "collab client: the host requires a token (give Token=...)";
          Disconnect();
          return false;
        }
        const std::string nonce = what.substr(std::strlen("CHALLENGE "));
        answered_ = true;
        authed_ = false;
        if (!WriteAll(BuildFrame(std::string(kCollabHandshakeTag) + "AUTH " + util::HmacSha256Hex(token_, nonce)), error)) {
          Disconnect();
          return false;
        }
      } else if (what == "OK") {
        authed_ = true;
        std::vector<std::string> held;
        held.swap(held_);
        for (const std::string& h : held) {
          std::string send_error;
          if (!Send(h, send_error)) { error = send_error; return false; }
        }
      } else if (what == "DENIED") {
        error = "collab client: the host rejected the token";
        Disconnect();
        return false;
      }
      continue;  // handshake frames are never data
    }
    out->push_back(std::move(payload));
  }
  if (!token_.empty() && !authed_ && !answered_ && NowMs() - connected_ms_ > auth_timeout_ms_) {
    error = "collab client: the host did not ask for the token (is it hosting without one?)";
    Disconnect();
    return false;
  }
  if (peer_closed) {
    error = "collab client: connection closed by peer";
    Disconnect();
    return false;
  }
  return true;
}

}  // namespace dino8::app
