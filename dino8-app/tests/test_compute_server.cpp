// Unit test for the minimal HTTP request server (src/net/ComputeServer.h)
// that closes real ground on PARITY_MAP.md's "Cloud/network compute service
// (Rhino.Compute equivalent)" item - before this there was no server/
// socket/HTTP code anywhere in the source.
//
// TryParseHttpRequest/BuildHttpResponse are pure functions tested first
// with in-memory byte strings - no socket involved - covering the request
// parsing edge cases (incomplete headers, a body that hasn't fully arrived
// yet, a request split across two partial reads). The final section is a
// genuine end-to-end test over a real loopback TCP socket: it starts a
// ComputeServer on an OS-assigned ephemeral port, connects a real client
// socket from a background thread, and proves PollOnce actually accepts,
// reads, dispatches to a handler and responds correctly - not just that
// the parsing helpers work in isolation.
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "net/ComputeServer.h"

using dino8::app::BuildHttpResponse;
using dino8::app::HttpRequest;
using dino8::app::HttpResponse;
using dino8::app::TryParseHttpRequest;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}  // namespace

int main() {
  // A complete GET request with no body.
  {
    HttpRequest req;
    size_t consumed = 0;
    const std::string buf = "GET /status HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const bool ok = TryParseHttpRequest(buf, &req, &consumed);
    Check(ok, "GET with no body parses");
    Check(req.method == "GET", "GET method parsed");
    Check(req.path == "/status", "GET path parsed");
    Check(req.body.empty(), "GET body is empty");
    Check(consumed == buf.size(), "GET consumed the whole buffer");
  }

  // A complete POST request with a Content-Length body.
  {
    HttpRequest req;
    size_t consumed = 0;
    const std::string body = "print(1+1)";
    const std::string buf = "POST /run HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    const bool ok = TryParseHttpRequest(buf, &req, &consumed);
    Check(ok, "POST with body parses");
    Check(req.method == "POST", "POST method parsed");
    Check(req.path == "/run", "POST path parsed");
    Check(req.body == body, "POST body matches Content-Length bytes exactly");
    Check(consumed == buf.size(), "POST consumed exactly headers+body, no trailing garbage assumed");
  }

  // Headers not yet fully received (no blank-line terminator) - must ask
  // for more data, not misparse a partial header block.
  {
    HttpRequest req;
    size_t consumed = 0;
    const bool ok = TryParseHttpRequest("POST /run HTTP/1.1\r\nContent-Length: 10\r\n", &req, &consumed);
    Check(!ok, "incomplete headers (no blank line yet) returns false");
  }

  // Headers complete but the declared body hasn't fully arrived - must
  // wait for the rest, not hand back a truncated body.
  {
    HttpRequest req;
    size_t consumed = 0;
    const bool ok = TryParseHttpRequest("POST /run HTTP/1.1\r\nContent-Length: 20\r\n\r\nshort", &req, &consumed);
    Check(!ok, "declared Content-Length longer than the body received so far returns false");
  }

  // The same request delivered as two partial chunks, exactly like a real
  // socket read loop would see it - proves the function is safe to call
  // repeatedly as bytes trickle in, not just on one fully-buffered blob.
  {
    const std::string body = "dino8.RunCommand('Box', '0,0,0', '1,1,1')";
    const std::string full = "POST /run HTTP/1.1\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    const std::string first_half = full.substr(0, full.size() / 2);
    const std::string second_half = full;
    HttpRequest req;
    size_t consumed = 0;
    Check(!TryParseHttpRequest(first_half, &req, &consumed), "a request split mid-stream is correctly reported incomplete");
    Check(TryParseHttpRequest(second_half, &req, &consumed), "the same request, once fully buffered, parses");
    Check(req.body == body, "the fully-buffered split request's body is intact");
  }

  // Response formatting.
  {
    HttpResponse resp;
    resp.status = 200;
    resp.body = "history: ok\n";
    const std::string wire = BuildHttpResponse(resp);
    Check(wire.rfind("HTTP/1.1 200 OK\r\n", 0) == 0, "response starts with the status line");
    Check(wire.find("Content-Length: 12\r\n") != std::string::npos, "response's Content-Length matches the body's byte length");
    Check(wire.find("Connection: close\r\n") != std::string::npos, "response declares Connection: close");
    Check(wire.rfind(resp.body) == wire.size() - resp.body.size(), "response ends with exactly the body");
  }
  {
    HttpResponse resp;
    resp.status = 500;
    resp.body = "! error\n";
    const std::string wire = BuildHttpResponse(resp);
    Check(wire.rfind("HTTP/1.1 500 Internal Server Error\r\n", 0) == 0, "a 500 response uses the right reason phrase");
  }
  {
    HttpResponse resp;
    resp.status = 401;
    resp.body = "nope\n";
    const std::string wire = BuildHttpResponse(resp);
    Check(wire.rfind("HTTP/1.1 401 Unauthorized\r\n", 0) == 0, "a 401 response uses the right reason phrase");
  }

  // Header parsing - the --serve-token bearer-auth check (main.cpp) reads
  // req.headers["authorization"], so TryParseHttpRequest has to surface
  // every header, not just Content-Length, and has to be case-insensitive
  // about both the header name (HTTP header names are case-insensitive)
  // and tolerant of the optional whitespace RFC 7230 allows around the
  // value - a real client (curl -H) sends "Authorization: Bearer x", but
  // nothing stops another client from writing "authorization:Bearer x" or
  // padding the value with trailing spaces.
  {
    HttpRequest req;
    size_t consumed = 0;
    const std::string buf =
        "POST /run HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer secret123\r\nContent-Length: 0\r\n\r\n";
    Check(TryParseHttpRequest(buf, &req, &consumed), "a request with an Authorization header parses");
    Check(req.headers.count("authorization") == 1, "the Authorization header is captured under its lowercased name");
    Check(req.headers.at("authorization") == "Bearer secret123", "the Authorization header's value is captured exactly");
    Check(req.headers.at("host") == "localhost", "every header is captured, not just Authorization/Content-Length");
  }
  {
    HttpRequest req;
    size_t consumed = 0;
    const std::string buf = "GET /run HTTP/1.1\r\nAUTHORIZATION:   Bearer  padded  \r\n\r\n";
    Check(TryParseHttpRequest(buf, &req, &consumed), "a request with an uppercase header name and padded value parses");
    Check(req.headers.count("authorization") == 1, "an all-caps header name is still matched case-insensitively");
    Check(req.headers.at("authorization") == "Bearer  padded", "only leading/trailing whitespace is trimmed, not interior spaces");
  }
  {
    HttpRequest req;
    size_t consumed = 0;
    const std::string buf = "GET /run HTTP/1.1\r\n\r\n";
    Check(TryParseHttpRequest(buf, &req, &consumed), "a request with no headers at all still parses");
    Check(req.headers.empty(), "no Authorization header means an empty headers map, not a spurious entry");
  }

#ifndef _WIN32
  // Real end-to-end round trip over an actual loopback TCP socket: start
  // the server on an OS-assigned port, connect a real client socket from a
  // background thread, exchange one request/response, and check both what
  // the handler received and what the client actually read back off the
  // wire.
  {
    dino8::app::ComputeServer server;
    std::string start_error;
    const bool started = server.Start(/*port=*/0, start_error);
    Check(started, "ComputeServer::Start(0) binds an OS-assigned ephemeral port");
    Check(server.Port() > 0, "Port() reports the real bound port after Start(0)");

    if (started) {
      std::string client_response;
      bool client_connected = false;
      std::thread client([&] {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<uint16_t>(server.Port()));
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
          ::close(fd);
          return;
        }
        client_connected = true;
        const std::string body = "hello";
        const std::string request = "POST /run HTTP/1.1\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        ::send(fd, request.data(), request.size(), 0);
        char buf[4096];
        ssize_t n;
        while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) client_response.append(buf, static_cast<size_t>(n));
        ::close(fd);
      });

      std::string handler_saw_body;
      std::string handler_saw_method;
      const bool serviced = server.PollOnce(
          [&](const HttpRequest& req) {
            handler_saw_method = req.method;
            handler_saw_body = req.body;
            HttpResponse resp;
            resp.body = "echo:" + req.body;
            return resp;
          },
          /*timeout_ms=*/2000);
      client.join();

      Check(client_connected, "the client thread connected to the real bound port");
      Check(serviced, "PollOnce serviced the incoming connection");
      Check(handler_saw_method == "POST", "the handler received the real POST method off the wire");
      Check(handler_saw_body == "hello", "the handler received the real request body off the wire");
      Check(client_response.find("echo:hello") != std::string::npos, "the client actually read the handler's response body back over the socket");
      Check(client_response.rfind("HTTP/1.1 200 OK", 0) == 0, "the client's response starts with a real HTTP status line");
    }

    server.Stop();
    Check(!server.IsRunning(), "Stop() leaves the server not running");
  }

  // No pending connection: PollOnce must return false (not block forever,
  // not spuriously report success) - and IsRunning()/Port() must both
  // reflect the not-yet-started state.
  {
    dino8::app::ComputeServer server;
    Check(!server.IsRunning(), "a freshly constructed ComputeServer is not running");
    Check(server.Port() == 0, "a freshly constructed ComputeServer reports port 0");
    std::string start_error;
    Check(server.Start(0, start_error), "a second, independent ComputeServer also starts on its own ephemeral port");
    bool handler_called = false;
    const bool serviced = server.PollOnce([&](const HttpRequest&) { handler_called = true; return HttpResponse{}; }, /*timeout_ms=*/50);
    Check(!serviced, "PollOnce with no pending connection returns false");
    Check(!handler_called, "PollOnce never calls the handler when there is nothing to service");
    server.Stop();
  }
#endif

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
