// One socket API, two spellings, shared by net/ComputeServer.cpp and net/Collab.cpp. Winsock2 and BSD/POSIX sockets share the same calls
// (socket/bind/listen/accept/select/recv/send/getsockname/connect) and the same sockaddr_in/htons/INADDR_LOOPBACK vocabulary; what differs is
//   (1) the handle type (SOCKET, an unsigned pointer-sized integer whose invalid value is INVALID_SOCKET, versus a non-negative int whose
//       invalid value is -1),
//   (2) how a socket is closed (closesocket versus close),
//   (3) where the error code lives (WSAGetLastError versus errno) and what "nothing right now" is called (WSAEWOULDBLOCK versus
//       EWOULDBLOCK/EAGAIN),
//   (4) how a socket is made non-blocking (ioctlsocket(FIONBIO) versus fcntl(O_NONBLOCK)) and the SO_RCVTIMEO option's value type (a DWORD of
//       milliseconds versus a timeval), and
//   (5) that Winsock has to be initialised once per process (WSAStartup) before any of it works.
// The tiny shim below pins each of those down so the server and client logic is written once, identically, for both.
//
// Header-only (every function is inline), so a standalone test target needs no extra source file; the only build requirement is linking
// ws2_32 on Windows (an OS-provided DLL). Not for public headers: it pulls in winsock2.h, which is why net/Collab.h and
// net/ComputeServer.h store a socket as an intptr_t and include nothing from here.
//
// Not wrapped, on purpose: SO_REUSEADDR. On POSIX it lets a restarted server rebind its port while the previous instance's connections are
// still in TIME_WAIT; on Winsock it means something else entirely (a second socket may bind the very same address while the first is still
// actively listening - port hijacking - with no TIME_WAIT benefit, since Windows allows that rebind anyway), so it is set by the caller
// behind `#ifndef _WIN32` and never on Windows. There is no SIGPIPE on Windows, so nothing to suppress there.
#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

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
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace dino8::app::sock {

#ifdef _WIN32

using Native = SOCKET;
constexpr Native kInvalid = INVALID_SOCKET;
// recv()/send() take and return int on Winsock, ssize_t/size_t on POSIX.
using IoLength = int;
using IoResult = int;

inline bool IsInvalid(Native s) { return s == INVALID_SOCKET; }
inline void Close(Native s) { ::closesocket(s); }

// WSAStartup once per process, the first time any socket user starts; the matching WSACleanup runs at process exit via the static's
// destructor. Thread-safe by C++11's magic-statics guarantee. A failed WSAStartup is reported by the caller's Start()/Connect() rather
// than silently proceeding to a socket() call that would then fail with a far less helpful WSANOTINITIALISED. `who` is the message
// prefix ("compute server", "collab server", ...).
inline bool EnsureWinsock(std::string& error, const char* who) {
  struct Winsock {
    int rc;
    WSADATA data{};
    Winsock() : rc(::WSAStartup(MAKEWORD(2, 2), &data)) {}
    ~Winsock() { if (rc == 0) ::WSACleanup(); }
  };
  static Winsock winsock;
  if (winsock.rc != 0) {
    error = std::string(who) + ": WSAStartup() failed: error " + std::to_string(winsock.rc);
    return false;
  }
  return true;
}

inline std::string LastError() {
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

// True when the call that just failed failed only because nothing can be read/written right now (a non-blocking socket).
inline bool WouldBlock() { return ::WSAGetLastError() == WSAEWOULDBLOCK; }

inline void SetNonBlocking(Native s) {
  u_long mode = 1;
  ::ioctlsocket(s, FIONBIO, &mode);
}

inline void SetReceiveTimeout(Native s, int seconds) {
  const DWORD ms = static_cast<DWORD>(seconds) * 1000u;
  ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
}

inline void SetNoDelay(Native s) {
  const int on = 1;
  ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
}

#else  // POSIX

using Native = int;
constexpr Native kInvalid = -1;
using IoLength = size_t;
using IoResult = ssize_t;

inline bool IsInvalid(Native s) { return s < 0; }
inline void Close(Native s) { ::close(s); }
inline bool EnsureWinsock(std::string&, const char*) { return true; }
inline std::string LastError() { return std::strerror(errno); }
inline bool WouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN; }

inline void SetNonBlocking(Native fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

inline void SetReceiveTimeout(Native s, int seconds) {
  timeval recv_tv{};
  recv_tv.tv_sec = seconds;
  recv_tv.tv_usec = 0;
  ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &recv_tv, sizeof(recv_tv));
}

inline void SetNoDelay(Native s) {
  const int on = 1;
  ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

#endif

// A socket is stored in the public headers as an intptr_t (so they need no socket headers at all): a POSIX fd round-trips through it
// unchanged, and a Winsock SOCKET (an unsigned pointer-sized handle) fits too - INVALID_SOCKET (all bits set) maps to -1, every valid
// handle to a non-negative value, which is exactly what the ">= 0" tests on those members need.
inline Native ToNative(std::intptr_t h) { return static_cast<Native>(h); }
inline std::intptr_t FromNative(Native s) { return static_cast<std::intptr_t>(s); }

// select()'s first argument is the highest fd + 1 on POSIX and ignored (kept only for source compatibility) on Winsock.
inline int SelectNfds(Native highest) { return static_cast<int>(highest) + 1; }

// Blocks for at most `ms` milliseconds until `s` can take more written data. False on timeout or error. Used instead of a busy loop when
// a non-blocking send() reports that the OS buffer is full.
inline bool WaitWritable(Native s, int ms) {
  fd_set write_fds;
  FD_ZERO(&write_fds);
  FD_SET(s, &write_fds);
  timeval tv{};
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  return ::select(SelectNfds(s), nullptr, &write_fds, nullptr, &tv) > 0;
}

// The largest slice handed to one send() call. A multi-megabyte snapshot is written in slices: Winsock can fail a single huge send() on a
// non-blocking socket with WSAENOBUFS where POSIX would simply take a partial write.
constexpr std::size_t kMaxSendSlice = 256u * 1024u;

}  // namespace dino8::app::sock
