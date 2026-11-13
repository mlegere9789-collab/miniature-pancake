#include "net/Collab.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <random>

#include "util/Hmac.h"

#ifndef _WIN32
#define DINO8_HAVE_POSIX_SOCKETS 1
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

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

#ifdef DINO8_HAVE_POSIX_SOCKETS
void SendAll(int fd, const std::string& payload) {
  const std::string wire = BuildFrame(payload);
  size_t sent = 0;
  while (sent < wire.size()) {
    const ssize_t n = ::send(fd, wire.data() + sent, wire.size() - sent, 0);
    if (n <= 0) break;
    sent += static_cast<size_t>(n);
  }
}

void SetNonBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
#endif

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
#ifndef DINO8_HAVE_POSIX_SOCKETS
  (void)port;
  (void)lan;
  error = "collab server: this build has no POSIX socket support (Windows is not yet supported - see net/Collab.h)";
  return false;
#else
  Stop();
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    error = std::string("collab server: socket() failed: ") + std::strerror(errno);
    return false;
  }
  const int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(lan ? INADDR_ANY : INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    error = std::string("collab server: bind() failed: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  if (::listen(fd, 16) != 0) {
    error = std::string("collab server: listen() failed: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  SetNonBlocking(fd);
  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  port_ = (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) ? ntohs(bound.sin_port) : port;
  listen_fd_ = fd;
  lan_ = lan;
  rejected_ = 0;
  return true;
#endif
}

void CollabServer::Stop() {
#ifdef DINO8_HAVE_POSIX_SOCKETS
  for (ClientConn& c : clients_) if (c.fd >= 0) ::close(c.fd);
  if (listen_fd_ >= 0) ::close(listen_fd_);
#endif
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

int CollabServer::PollOnce(int timeout_ms) {
#ifndef DINO8_HAVE_POSIX_SOCKETS
  (void)timeout_ms;
  return 0;
#else
  if (listen_fd_ < 0) return 0;

  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(listen_fd_, &read_fds);
  int max_fd = listen_fd_;
  for (const ClientConn& c : clients_) {
    if (c.fd < 0) continue;
    FD_SET(c.fd, &read_fds);
    max_fd = std::max(max_fd, c.fd);
  }
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  const int ready = ::select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
  if (ready <= 0) {
    // nothing to read, but idle unauthenticated connections still expire
    const long long t = NowMs();
    bool any = false;
    for (ClientConn& c : clients_) {
      if (c.fd >= 0 && !c.authed && t - c.accepted_ms > auth_timeout_ms_) { ::close(c.fd); c.fd = -1; ++rejected_; any = true; }
    }
    if (any) clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const ClientConn& c) { return c.fd < 0; }), clients_.end());
    return 0;
  }

  // Accept at most one new connection per call - plenty for a per-frame
  // poll loop (another will be picked up next call), and keeps this
  // symmetric with ComputeServer::PollOnce's own "service one thing" shape.
  if (FD_ISSET(listen_fd_, &read_fds)) {
    const int conn_fd = ::accept(listen_fd_, nullptr, nullptr);
    if (conn_fd >= 0) {
      SetNonBlocking(conn_fd);
      ClientConn conn;
      conn.fd = conn_fd;
      conn.accepted_ms = NowMs();
      if (!token_.empty()) {
        conn.authed = false;
        conn.nonce = RandomNonceHex();
        SendAll(conn_fd, std::string(kCollabHandshakeTag) + "CHALLENGE " + conn.nonce);
      }
      clients_.push_back(std::move(conn));
    }
  }

  // Read whatever is ready from every existing client.
  std::vector<bool> drop(clients_.size(), false);
  char chunk[65536];
  for (size_t i = 0; i < clients_.size(); ++i) {
    ClientConn& c = clients_[i];
    if (c.fd < 0 || !FD_ISSET(c.fd, &read_fds)) continue;
    while (true) {
      const ssize_t n = ::recv(c.fd, chunk, sizeof(chunk), 0);
      if (n > 0) {
        c.inbuf.append(chunk, static_cast<size_t>(n));
        if (c.inbuf.size() > static_cast<size_t>(kMaxFrameBytes) + 4) { drop[i] = true; break; }
        continue;
      }
      if (n == 0) { drop[i] = true; }  // peer closed
      break;  // n < 0: EWOULDBLOCK/EAGAIN (nothing more right now) or a real error - either way, stop reading
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
          SendAll(sender.fd, std::string(kCollabHandshakeTag) + "OK");
        } else {
          SendAll(sender.fd, std::string(kCollabHandshakeTag) + "DENIED");
          drop[i] = true;
          ++rejected_;
        }
        continue;
      }
      const std::string wire = BuildFrame(payload);
      for (size_t j = 0; j < clients_.size(); ++j) {
        if (j == i || clients_[j].fd < 0 || drop[j] || !clients_[j].authed) continue;
        size_t sent = 0;
        while (sent < wire.size()) {
          const ssize_t n = ::send(clients_[j].fd, wire.data() + sent, wire.size() - sent, 0);
          if (n <= 0) break;  // a stalled peer loses this relay rather than blocking everyone else
          sent += static_cast<size_t>(n);
        }
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
    if (drop[i] && clients_[i].fd >= 0) { ::close(clients_[i].fd); clients_[i].fd = -1; }
  }
  clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const ClientConn& c) { return c.fd < 0; }),
                  clients_.end());
  return relayed;
#endif
}

// ---- CollabClient ------------------------------------------------------

CollabClient::~CollabClient() { Disconnect(); }

bool CollabClient::Connect(const std::string& host, int port, std::string& error) {
#ifndef DINO8_HAVE_POSIX_SOCKETS
  (void)host;
  (void)port;
  error = "collab client: this build has no POSIX socket support (Windows is not yet supported - see net/Collab.h)";
  return false;
#else
  Disconnect();
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    error = std::string("collab client: socket() failed: ") + std::strerror(errno);
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    error = "collab client: '" + host + "' is not a numeric IPv4 address";
    ::close(fd);
    return false;
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    error = std::string("collab client: connect() failed: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  SetNonBlocking(fd);
  fd_ = fd;
  inbuf_.clear();
  authed_ = false;
  answered_ = false;
  held_.clear();
  connected_ms_ = NowMs();
  return true;
#endif
}

void CollabClient::Disconnect() {
#ifdef DINO8_HAVE_POSIX_SOCKETS
  if (fd_ >= 0) ::close(fd_);
#endif
  fd_ = -1;
  inbuf_.clear();
  held_.clear();
}

bool CollabClient::Send(const std::string& payload, std::string& error) {
#ifndef DINO8_HAVE_POSIX_SOCKETS
  (void)payload;
  error = "collab client: not connected (no POSIX socket support)";
  return false;
#else
  if (fd_ < 0) {
    error = "collab client: not connected";
    return false;
  }
  if (!IsAuthenticated()) { held_.push_back(payload); return true; }  // sent once the host accepts the token
  const std::string wire = BuildFrame(payload);
  size_t sent = 0;
  while (sent < wire.size()) {
    const ssize_t n = ::send(fd_, wire.data() + sent, wire.size() - sent, 0);
    if (n > 0) { sent += static_cast<size_t>(n); continue; }
    if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) continue;  // small frame, keep trying
    error = std::string("collab client: send() failed: ") + std::strerror(errno);
    return false;
  }
  return true;
#endif
}

bool CollabClient::PollOnce(std::vector<std::string>* out, std::string& error) {
#ifndef DINO8_HAVE_POSIX_SOCKETS
  (void)out;
  error = "collab client: not connected (no POSIX socket support)";
  return false;
#else
  if (fd_ < 0) {
    error = "collab client: not connected";
    return false;
  }
  bool peer_closed = false;
  char chunk[65536];
  while (true) {
    const ssize_t n = ::recv(fd_, chunk, sizeof(chunk), 0);
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
    if (errno == EWOULDBLOCK || errno == EAGAIN) break;  // nothing more right now - not an error
    error = std::string("collab client: recv() failed: ") + std::strerror(errno);
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
        const std::string wire = BuildFrame(std::string(kCollabHandshakeTag) + "AUTH " + util::HmacSha256Hex(token_, nonce));
        size_t sent = 0;
        while (sent < wire.size()) {
          const ssize_t n = ::send(fd_, wire.data() + sent, wire.size() - sent, 0);
          if (n > 0) { sent += static_cast<size_t>(n); continue; }
          if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) continue;
          error = std::string("collab client: send() failed: ") + std::strerror(errno);
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
#endif
}

}  // namespace dino8::app
