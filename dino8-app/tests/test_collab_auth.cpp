// Token authentication for the collaboration relay (net/Collab.h): HMAC-SHA256 challenge/response, rejection of a wrong or missing
// token, frames from an unauthenticated connection never relayed, an idle unauthenticated connection dropped, and the opt-in LAN bind.
// Standalone (Collab.cpp + Sha256.cpp only), over real loopback sockets, like tests/test_collab.cpp.
//
// Independent expected values: the HMAC-SHA256 outputs are the published RFC 4231 test vectors (cases 1, 2 and 6, the last with a key
// longer than the 64-byte block), not values computed by the code under test.
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "net/Collab.h"
#include "util/Hmac.h"

using dino8::app::BuildFrame;
using dino8::app::CollabClient;
using dino8::app::CollabServer;

namespace {
// Test tokens are generated at run time (never a literal in the repository): 16 random hex characters, distinct on every call.
std::string GenToken() {
  static const char* digits = "0123456789abcdef";
  std::random_device rd;
  std::string out;
  for (int i = 0; i < 16; ++i) out.push_back(digits[rd() & 15]);
  return out;
}
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}
bool Contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

// Services the relay and every client until `done` holds (or ~3 s pass). Frames each client received accumulate in `got[i]`.
struct World {
  CollabServer* server = nullptr;
  std::vector<CollabClient*> clients;
  std::vector<std::vector<std::string>> got;
  std::vector<std::string> err;
  std::vector<bool> alive;
  void Init() { got.assign(clients.size(), {}); err.assign(clients.size(), ""); alive.assign(clients.size(), true); }
  template <class F>
  bool Pump(F done, int max_ms = 3000) {
    for (int t = 0; t < max_ms; t += 5) {
      if (server) server->PollOnce(0);
      for (size_t i = 0; i < clients.size(); ++i) {
        if (!alive[i]) continue;
        std::string e;
        if (!clients[i]->PollOnce(&got[i], e)) { alive[i] = false; err[i] = e; }
      }
      if (done()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
  }
};
bool Has(const std::vector<std::string>& v, const std::string& s) {
  for (const std::string& x : v) if (x == s) return true;
  return false;
}
}  // namespace

int main() {
  using dino8::util::HmacSha256Hex;
  // ---- HMAC-SHA256, RFC 4231 ----
  Check(HmacSha256Hex(std::string(20, '\x0b'), "Hi There") == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", "RFC 4231 test case 1");
  Check(HmacSha256Hex("Jefe", "what do ya want for nothing?") == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", "RFC 4231 test case 2");
  Check(HmacSha256Hex(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First") ==
            "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
        "RFC 4231 test case 6 (key longer than the block size)");
  Check(dino8::util::ConstantTimeEqual("abc", "abc") && !dino8::util::ConstantTimeEqual("abc", "abd") && !dino8::util::ConstantTimeEqual("abc", "ab"), "ConstantTimeEqual");

#ifndef _WIN32
  const std::string token = GenToken();
  const std::string wrong_token = GenToken();
  Check(token != wrong_token, "the two generated test tokens differ");
  // ---- 1. authenticated relay ----
  {
    CollabServer server;
    server.SetToken(token);
    std::string e;
    Check(server.Start(0, e), "token server starts: " + e);
    Check(server.AuthRequired(), "the server reports that a token is required");
    Check(server.BoundAddress() == "127.0.0.1" && !server.Lan(), "default bind is loopback only");
    CollabClient a, b;
    a.SetToken(token); b.SetToken(token);
    Check(a.Connect("127.0.0.1", server.Port(), e) && b.Connect("127.0.0.1", server.Port(), e), "two token clients connect");
    Check(!a.IsAuthenticated(), "a token client is not authenticated before the handshake");
    World w; w.server = &server; w.clients = {&a, &b}; w.Init();
    Check(w.Pump([&] { return a.IsAuthenticated() && b.IsAuthenticated(); }), "both clients authenticate");
    Check(server.AuthedCount() == 2 && server.RejectedCount() == 0, "the relay counts 2 authenticated, 0 rejected");
    std::string se;
    a.Send("hello-from-A", se);
    Check(w.Pump([&] { return Has(w.got[1], "hello-from-A"); }), "a frame from an authenticated client reaches the other one");
    Check(!Has(w.got[0], "hello-from-A"), "... and is not echoed back to its sender");

    // ---- 2. wrong token ----
    CollabClient c;
    c.SetToken(wrong_token);
    c.Connect("127.0.0.1", server.Port(), e);
    c.Send("evil-from-C", se);  // queued by the client, must never be sent unauthenticated
    World w2; w2.server = &server; w2.clients = {&a, &b, &c}; w2.Init();
    Check(w2.Pump([&] { return !w2.alive[2]; }), "a client with the wrong token is cut off");
    Check(Contains(w2.err[2], "rejected"), "... with an error naming the rejection: " + w2.err[2]);
    Check(server.RejectedCount() == 1, "the relay counts 1 rejected connection");
    w2.Pump([] { return false; }, 150);
    Check(!Has(w2.got[0], "evil-from-C") && !Has(w2.got[1], "evil-from-C"), "nothing from the rejected client was relayed");
    a.Send("after-reject", se);
    Check(w2.Pump([&] { return Has(w2.got[1], "after-reject"); }), "the session keeps working for the authenticated clients");

    // ---- 3. a raw socket that skips the handshake ----
    const int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(server.Port()));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    Check(::connect(raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "a raw socket connects");
    const std::string wire = BuildFrame("raw-evil-frame");
    ::send(raw, wire.data(), wire.size(), 0);
    Check(w2.Pump([&] { return server.RejectedCount() == 2; }), "a frame sent without the handshake gets the connection rejected (2 rejected)");
    w2.Pump([] { return false; }, 150);
    Check(!Has(w2.got[0], "raw-evil-frame") && !Has(w2.got[1], "raw-evil-frame"), "the unauthenticated frame was not relayed");
    ::close(raw);

    // ---- 4. no token against a token server ----
    CollabClient d;
    d.Connect("127.0.0.1", server.Port(), e);
    World w3; w3.server = &server; w3.clients = {&d}; w3.Init();
    Check(w3.Pump([&] { return !w3.alive[0]; }), "a client with no token is told one is needed");
    Check(Contains(w3.err[0], "requires a token"), "... with the error: " + w3.err[0]);
  }
  // ---- 5. an idle unauthenticated connection is dropped ----
  {
    CollabServer server;
    server.SetToken(token);
    server.SetAuthTimeoutMs(150);
    std::string e;
    server.Start(0, e);
    const int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(server.Port()));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ::connect(raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    World w; w.server = &server; w.Init();
    w.Pump([&] { return server.ClientCount() == 1; }, 500);
    Check(server.ClientCount() == 1, "the idle connection is accepted first");
    Check(w.Pump([&] { return server.ClientCount() == 0; }, 1500), "... and dropped once it has not authenticated within the timeout");
    ::close(raw);
  }
  // ---- 6. no token at all: the old behaviour is unchanged ----
  {
    CollabServer server;
    std::string e;
    Check(server.Start(0, e) && !server.AuthRequired(), "a tokenless server starts and requires nothing");
    CollabClient a, b;
    a.Connect("127.0.0.1", server.Port(), e); b.Connect("127.0.0.1", server.Port(), e);
    Check(a.IsAuthenticated(), "a tokenless client counts as authenticated");
    World w; w.server = &server; w.clients = {&a, &b}; w.Init();
    w.Pump([&] { return server.ClientCount() == 2; });
    std::string se;
    a.Send("plain", se);
    Check(w.Pump([&] { return Has(w.got[1], "plain"); }), "frames relay with no handshake, exactly as before");
  }
  // ---- 7. a token client against a tokenless server does not hang silently ----
  {
    CollabServer server;
    std::string e;
    server.Start(0, e);
    CollabClient a;
    a.SetToken(token);
    a.SetAuthTimeoutMs(200);
    a.Connect("127.0.0.1", server.Port(), e);
    World w; w.server = &server; w.clients = {&a}; w.Init();
    Check(w.Pump([&] { return !w.alive[0]; }, 2000), "a token client gives up when the host never asks for the token");
    Check(Contains(w.err[0], "did not ask for the token"), "... with the error: " + w.err[0]);
  }
  // ---- 8. the LAN bind ----
  {
    CollabServer server;
    server.SetToken(token);
    std::string e;
    Check(server.Start(0, e, true), "a LAN-bound token server starts: " + e);
    Check(server.Lan() && server.BoundAddress() == "0.0.0.0", "it listens on every interface (bound address 0.0.0.0)");
    CollabClient a;
    a.SetToken(token);
    Check(a.Connect("127.0.0.1", server.Port(), e), "a client still connects to it");
    World w; w.server = &server; w.clients = {&a}; w.Init();
    Check(w.Pump([&] { return a.IsAuthenticated(); }), "and authenticates");
  }
#endif
  std::printf("%s\n", failures ? "SOME CHECKS FAILED" : "all collab auth checks passed");
  return failures ? 1 : 0;
}
