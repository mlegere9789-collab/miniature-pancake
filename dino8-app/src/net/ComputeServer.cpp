#include "net/ComputeServer.h"

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>

// One socket API, two spellings. Winsock2 and BSD/POSIX sockets share the
// same calls (socket/bind/listen/accept/select/recv/send/getsockname) and
// the same sockaddr_in/htons/INADDR_LOOPBACK vocabulary; what differs is
// (1) the handle type (SOCKET, an unsigned pointer-sized integer whose
// invalid value is INVALID_SOCKET, versus a non-negative int whose invalid
// value is -1), (2) how a socket is closed (closesocket versus close),
// (3) where the error code lives (WSAGetLastError versus errno), (4) the
// SO_RCVTIMEO option's value type (a DWORD of milliseconds versus a
// timeval), and (5) that Winsock has to be initialised once per process
// (WSAStartup) before any of it works. The tiny shim below pins each of
// those five down so the server logic itself is written once, identically,
// for both.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
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

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
// recv()/send() take and return int on Winsock, ssize_t/size_t on POSIX.
using IoLength = int;
using IoResult = int;

bool IsInvalid(NativeSocket s) { return s == INVALID_SOCKET; }
void CloseSocket(NativeSocket s) { ::closesocket(s); }

// WSAStartup once per process, the first time any ComputeServer starts;
// the matching WSACleanup runs at process exit via the static's destructor.
// Thread-safe by C++11's magic-statics guarantee. A failed WSAStartup is
// reported by Start() rather than silently proceeding to a socket() call
// that would then fail with a far less helpful WSANOTINITIALISED.
bool EnsureWinsock(std::string& error) {
  struct Winsock {
    int rc;
    WSADATA data{};
    Winsock() : rc(::WSAStartup(MAKEWORD(2, 2), &data)) {}
    ~Winsock() { if (rc == 0) ::WSACleanup(); }
  };
  static Winsock winsock;
  if (winsock.rc != 0) {
    error = "compute server: WSAStartup() failed: error " + std::to_string(winsock.rc);
    return false;
  }
  return true;
}

std::string LastSocketError() {
  const int code = ::WSAGetLastError();
  char* msg = nullptr;
  const DWORD n = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<char*>(&msg), 0, nullptr);
  std::string text = (n && msg) ? std::string(msg, n) : std::string();
  if (msg) ::LocalFree(msg);
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) text.pop_back();
  if (text.empty()) text = "error";
  return text + " (" + std::to_string(code) + ")";
}

void SetReceiveTimeout(NativeSocket s, int seconds) {
  const DWORD ms = static_cast<DWORD>(seconds) * 1000u;
  ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
using IoLength = size_t;
using IoResult = ssize_t;

bool IsInvalid(NativeSocket s) { return s < 0; }
void CloseSocket(NativeSocket s) { ::close(s); }
bool EnsureWinsock(std::string&) { return true; }
std::string LastSocketError() { return std::strerror(errno); }

void SetReceiveTimeout(NativeSocket s, int seconds) {
  timeval recv_tv{};
  recv_tv.tv_sec = seconds;
  recv_tv.tv_usec = 0;
  ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &recv_tv, sizeof(recv_tv));
}
#endif

// The listening socket is stored in the header as an intptr_t (so the
// header needs no socket headers at all): a POSIX fd round-trips through it
// unchanged, and a Winsock SOCKET (an unsigned pointer-sized handle) fits
// too - INVALID_SOCKET (all bits set) maps to -1, every valid handle to a
// non-negative value, which is exactly what IsRunning()'s ">= 0" test needs.
NativeSocket ToNative(intptr_t h) { return static_cast<NativeSocket>(h); }
intptr_t FromNative(NativeSocket s) { return static_cast<intptr_t>(s); }

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
  std::map<std::string, std::string> headers;
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
      size_t vend = header_line.size();
      while (vend > vstart && (header_line[vend - 1] == ' ' || header_line[vend - 1] == '\t')) --vend;
      const std::string value = header_line.substr(vstart, vend - vstart);
      headers[key] = value;
      if (key == "content-length") {
        // strtoul accepts a leading '-' and negates the parsed value per
        // the C standard, so "Content-Length: -1" would otherwise become
        // SIZE_MAX/ULONG_MAX; added to body_start below, that overflows
        // size_t and can wrap back under buffer.size(), making the
        // "body not fully received yet" check below pass on a request
        // whose body hasn't actually arrived. Reject anything that isn't a
        // plain non-negative number, or that already exceeds the request
        // size cap, instead of letting it wrap.
        bool all_digits = !value.empty();
        for (unsigned char c : value) all_digits = all_digits && std::isdigit(c);
        if (!all_digits) return false;
        errno = 0;
        const unsigned long parsed = std::strtoul(value.c_str(), nullptr, 10);
        if (errno == ERANGE || parsed > kMaxRequestBytes) return false;
        content_length = parsed;
      }
    }
    pos = next + 2;
  }

  const size_t body_start = header_end + 4;
  if (buffer.size() < body_start + content_length) return false;  // body not fully received yet

  out->method = method;
  out->path = path;
  out->body = buffer.substr(body_start, content_length);
  out->headers = std::move(headers);
  *consumed = body_start + content_length;
  return true;
}

std::string BuildHttpResponse(const HttpResponse& resp) {
  const char* reason = "OK";
  switch (resp.status) {
    case 200: reason = "OK"; break;
    case 400: reason = "Bad Request"; break;
    case 401: reason = "Unauthorized"; break;
    case 404: reason = "Not Found"; break;
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
  Stop();
  if (!EnsureWinsock(error)) return false;
  const NativeSocket fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (IsInvalid(fd)) {
    error = "compute server: socket() failed: " + LastSocketError();
    return false;
  }
#ifndef _WIN32
  // Lets a restarted server rebind its port while the previous instance's
  // connections are still in TIME_WAIT. Deliberately NOT set on Winsock,
  // where SO_REUSEADDR means something else entirely: it lets a second
  // socket bind the very same address while the first is still actively
  // listening (port hijacking), with no TIME_WAIT benefit (Windows allows
  // that rebind anyway).
  const int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    error = "compute server: bind() failed: " + LastSocketError();
    CloseSocket(fd);
    return false;
  }
  if (::listen(fd, 8) != 0) {
    error = "compute server: listen() failed: " + LastSocketError();
    CloseSocket(fd);
    return false;
  }
  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  port_ = (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) ? ntohs(bound.sin_port) : port;
  listen_fd_ = FromNative(fd);
  return true;
}

void ComputeServer::Stop() {
  if (listen_fd_ >= 0) CloseSocket(ToNative(listen_fd_));
  listen_fd_ = -1;
  port_ = 0;
}

bool ComputeServer::PollOnce(const ComputeHandler& handler, int timeout_ms) {
  if (listen_fd_ < 0) return false;
  const NativeSocket listen_fd = ToNative(listen_fd_);

  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(listen_fd, &read_fds);
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  // select()'s first argument is the highest fd + 1 on POSIX and ignored
  // (kept only for source compatibility) on Winsock.
  const int ready = ::select(static_cast<int>(listen_fd + 1), &read_fds, nullptr, nullptr, &tv);
  if (ready <= 0) return false;

  const NativeSocket conn_fd = ::accept(listen_fd, nullptr, nullptr);
  if (IsInvalid(conn_fd)) return false;

  // A slow/stalled client can't hang this call forever: cap both the total
  // time spent reading one request (a receive timeout on the socket) and
  // its total size (kMaxRequestBytes, checked each time around the loop).
  SetReceiveTimeout(conn_fd, 5);

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
    const IoResult n = ::recv(conn_fd, chunk, static_cast<IoLength>(sizeof(chunk)), 0);
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
    const IoResult n = ::send(conn_fd, wire.data() + sent, static_cast<IoLength>(wire.size() - sent), 0);
    if (n <= 0) break;
    sent += static_cast<size_t>(n);
  }
  CloseSocket(conn_fd);
  return true;
}

}  // namespace dino8::app
