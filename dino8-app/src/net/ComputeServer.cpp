#include "net/ComputeServer.h"

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>

#ifndef _WIN32
#define DINO8_HAVE_POSIX_SOCKETS 1
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace dino8::app {

namespace {

std::string ToLowerAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// A request larger than this (headers + declared body) is refused outright
// rather than buffered indefinitely - this is a local automation service,
// not a public-facing endpoint hardened against arbitrary request sizes.
constexpr size_t kMaxRequestBytes = 8 * 1024 * 1024;

}  // namespace

bool TryParseHttpRequest(const std::string& buffer, HttpRequest* out, size_t* consumed) {
  const size_t header_end = buffer.find("\r\n\r\n");
  if (header_end == std::string::npos) return false;

  const size_t line_end = buffer.find("\r\n");
  if (line_end == std::string::npos || line_end > header_end) return false;
  std::istringstream request_line(buffer.substr(0, line_end));
  std::string method, path, version;
  if (!(request_line >> method >> path >> version)) return false;

  size_t content_length = 0;
  size_t pos = line_end + 2;
  while (pos < header_end) {
    size_t next = buffer.find("\r\n", pos);
    if (next == std::string::npos || next > header_end) next = header_end;
    const std::string header_line = buffer.substr(pos, next - pos);
    const size_t colon = header_line.find(':');
    if (colon != std::string::npos) {
      const std::string key = ToLowerAscii(header_line.substr(0, colon));
      size_t vstart = colon + 1;
      while (vstart < header_line.size() && header_line[vstart] == ' ') ++vstart;
      if (key == "content-length") {
        content_length = static_cast<size_t>(std::strtoul(header_line.c_str() + vstart, nullptr, 10));
      }
    }
    pos = next + 2;
  }

  const size_t body_start = header_end + 4;
  if (buffer.size() < body_start + content_length) return false;  // body not fully received yet

  out->method = method;
  out->path = path;
  out->body = buffer.substr(body_start, content_length);
  *consumed = body_start + content_length;
  return true;
}

std::string BuildHttpResponse(const HttpResponse& resp) {
  const char* reason = "OK";
  switch (resp.status) {
    case 200: reason = "OK"; break;
    case 400: reason = "Bad Request"; break;
    case 405: reason = "Method Not Allowed"; break;
    case 413: reason = "Payload Too Large"; break;
    case 500: reason = "Internal Server Error"; break;
    default: reason = "OK"; break;
  }
  std::ostringstream out;
  out << "HTTP/1.1 " << resp.status << ' ' << reason << "\r\n"
      << "Content-Type: " << resp.content_type << "\r\n"
      << "Content-Length: " << resp.body.size() << "\r\n"
      << "Connection: close\r\n"
      << "\r\n"
      << resp.body;
  return out.str();
}

ComputeServer::~ComputeServer() { Stop(); }

bool ComputeServer::Start(int port, std::string& error) {
#ifndef DINO8_HAVE_POSIX_SOCKETS
  (void)port;
  error = "compute server: this build has no POSIX socket support (Windows is not yet supported - see net/ComputeServer.h)";
  return false;
#else
  Stop();
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    error = std::string("compute server: socket() failed: ") + std::strerror(errno);
    return false;
  }
  const int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    error = std::string("compute server: bind() failed: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  if (::listen(fd, 8) != 0) {
    error = std::string("compute server: listen() failed: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  port_ = (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) ? ntohs(bound.sin_port) : port;
  listen_fd_ = fd;
  return true;
#endif
}

void ComputeServer::Stop() {
#ifdef DINO8_HAVE_POSIX_SOCKETS
  if (listen_fd_ >= 0) ::close(listen_fd_);
#endif
  listen_fd_ = -1;
  port_ = 0;
}

bool ComputeServer::PollOnce(const ComputeHandler& handler, int timeout_ms) {
#ifndef DINO8_HAVE_POSIX_SOCKETS
  (void)handler;
  (void)timeout_ms;
  return false;
#else
  if (listen_fd_ < 0) return false;

  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(listen_fd_, &read_fds);
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  const int ready = ::select(listen_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
  if (ready <= 0) return false;

  const int conn_fd = ::accept(listen_fd_, nullptr, nullptr);
  if (conn_fd < 0) return false;

  // A slow/stalled client can't hang this call forever: cap both the total
  // time spent reading one request (a receive timeout on the socket) and
  // its total size (kMaxRequestBytes, checked each time around the loop).
  timeval recv_tv{};
  recv_tv.tv_sec = 5;
  recv_tv.tv_usec = 0;
  ::setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &recv_tv, sizeof(recv_tv));

  std::string buffer;
  HttpRequest request;
  size_t consumed = 0;
  bool parsed = false;
  bool oversized = false;
  char chunk[4096];
  while (true) {
    if (TryParseHttpRequest(buffer, &request, &consumed)) {
      parsed = true;
      break;
    }
    if (buffer.size() > kMaxRequestBytes) {
      oversized = true;
      break;
    }
    const ssize_t n = ::recv(conn_fd, chunk, sizeof(chunk), 0);
    if (n <= 0) break;  // client closed early, or the receive timeout above fired
    buffer.append(chunk, static_cast<size_t>(n));
  }

  HttpResponse response;
  if (oversized) {
    response.status = 413;
    response.body = "request too large\n";
  } else if (!parsed) {
    response.status = 400;
    response.body = "incomplete or malformed HTTP request\n";
  } else {
    response = handler(request);
  }
  const std::string wire = BuildHttpResponse(response);
  size_t sent = 0;
  while (sent < wire.size()) {
    const ssize_t n = ::send(conn_fd, wire.data() + sent, wire.size() - sent, 0);
    if (n <= 0) break;
    sent += static_cast<size_t>(n);
  }
  ::close(conn_fd);
  return true;
#endif
}

}  // namespace dino8::app
