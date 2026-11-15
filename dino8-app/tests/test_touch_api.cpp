// Unit test for the Application-free half of the Dino 8 Touch server side
// (src/net/TouchGate.h/.cpp): the parts that decide whether a request may
// reach the app at all. Before this, token auth, the command blocklist and the
// /touch/ path whitelist were only exercised by the Playwright browser test
// (touch/tests/touch_gestures.test.js), which needs a window, node and
// chromium; this runs anywhere ctest does.
//
// Covers:
//   * bearer-token auth: a missing header, a wrong token, a wrong scheme, a
//     prefix/extension of the right token are refused; the right token (plain,
//     named, and a "sha256:HEX" file entry) is accepted and yields the caller
//     name; with no candidates nothing matches;
//   * the 401 (wrong token) and 403 (no token configured) answers of the touch
//     API;
//   * the --serve-lan token requirement;
//   * the command blocklist (script/plug-in loaders and the "=" evaluator,
//     case-insensitive, with command-prefix characters) and that ordinary
//     drawing commands pass;
//   * /touch/ routing predicates and the static-file server's path
//     whitelist: ".." traversal (plain, encoded, backslash, absolute-path
//     smuggling, hidden files, non-whitelisted extensions, non-GET methods)
//     are refused, legitimate files are served with the security headers.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "net/TouchGate.h"
#include "util/Sha256.h"

using namespace dino8::app;

namespace {
int failures = 0;
int checks = 0;
void Check(bool ok, const char* what) {
  ++checks;
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
void Check(bool ok, const std::string& what) { Check(ok, what.c_str()); }

HttpRequest Req(const std::string& method, const std::string& path, const std::string& auth = "") {
  HttpRequest r;
  r.method = method;
  r.path = path;
  if (!auth.empty()) r.headers["authorization"] = auth;
  return r;
}

void WriteFile(const std::filesystem::path& p, const std::string& body) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f << body;
}
}  // namespace

int main() {
  using Candidates = std::vector<std::pair<std::string, std::string>>;

  // ---- bearer-token auth --------------------------------------------------
  {
    const std::string tok = "0123456789abcdef0123456789abcdef";
    const Candidates one = {{"touch", tok}};
    std::string who;
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping"), one, who), "no Authorization header is refused");
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", "Bearer wrong"), one, who), "wrong token is refused");
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", "Bearer "), one, who), "empty bearer token is refused");
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", "Bearer " + tok.substr(0, tok.size() - 1)), one, who),
          "a prefix of the right token is refused");
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", "Bearer " + tok + "0"), one, who),
          "the right token plus one byte is refused");
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", "Basic " + tok), one, who), "wrong auth scheme is refused");
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", tok), one, who), "bare token without 'Bearer ' is refused");
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", "bearer " + tok), one, who), "lowercase scheme is refused (exact 'Bearer ')");
    Check(who.empty(), "caller name untouched by refused attempts");
    Check(MatchBearerToken(Req("GET", "/touch/api/ping", "Bearer " + tok), one, who), "the right token is accepted");
    Check(who == "touch", "accepted token reports its caller name");

    const Candidates many = {{"ci", "aaaa"}, {"", "bbbb"}, {"phone", "cccc"}};
    who = "unset";
    Check(MatchBearerToken(Req("POST", "/touch/api/command", "Bearer cccc"), many, who) && who == "phone",
          "any one of several tokens is accepted, with its own name");
    Check(MatchBearerToken(Req("POST", "/touch/api/command", "Bearer bbbb"), many, who) && who.empty(),
          "an anonymous (unnamed) token is accepted with an empty caller name");
    Check(!MatchBearerToken(Req("POST", "/touch/api/command", "Bearer dddd"), many, who), "a token that is none of them is refused");

    const Candidates none;
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", "Bearer anything"), none, who), "with no candidates nothing matches");

    // A token FILE may hold only the digest ("sha256:HEX"): the digest itself
    // must not be accepted as the bearer secret, the preimage must be.
    const std::string secret = "tablet-secret-123";
    const std::string digest = dino8::util::Sha256Hex(secret);
    const Candidates hashed = {{"tablet", "sha256:" + digest}};
    Check(MatchBearerToken(Req("GET", "/touch/api/ping", "Bearer " + secret), hashed, who) && who == "tablet",
          "a sha256: entry accepts the token that hashes to it");
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", "Bearer " + digest), hashed, who),
          "a sha256: entry does not accept its own digest as the token");
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", "Bearer sha256:" + digest), hashed, who),
          "a sha256: entry does not accept 'sha256:<digest>' as the token");
    Check(!MatchBearerToken(Req("GET", "/touch/api/ping", "Bearer wrong"), hashed, who), "a sha256: entry refuses a wrong token");

    Check(ConstantTimeEquals("abc", "abc"), "ConstantTimeEquals: equal strings");
    Check(!ConstantTimeEquals("abc", "abd"), "ConstantTimeEquals: differing last byte");
    Check(!ConstantTimeEquals("abc", "abcd"), "ConstantTimeEquals: differing length");
    Check(ConstantTimeEquals("", ""), "ConstantTimeEquals: both empty");
  }

  // ---- touch API refusals -------------------------------------------------
  {
    const HttpResponse u = TouchUnauthorizedResponse();
    Check(u.status == 401, "wrong/missing token answers 401");
    Check(u.content_type == "application/json" && u.body.find("\"ok\":false") != std::string::npos &&
              u.body.find("pairing token") != std::string::npos,
          "401 body is the JSON error naming the pairing token");
    Check(u.headers.count("Access-Control-Allow-Origin") == 1 && u.headers.at("Cache-Control") == "no-store",
          "401 carries the CORS origin header (so the PWA can read it) and no-store");
    const HttpResponse f = TouchNoTokenResponse();
    Check(f.status == 403, "no token configured at all answers 403 (the touch API never runs open)");
    Check(f.body.find("\"ok\":false") != std::string::npos && f.body.find("--serve-touch") != std::string::npos,
          "403 body is the JSON error that says how to configure a token");
    const HttpResponse p = TouchPreflight();
    Check(p.status == 204 && p.headers.count("Access-Control-Allow-Headers") == 1 &&
              p.headers.at("Access-Control-Allow-Headers").find("Authorization") != std::string::npos,
          "CORS preflight answers 204 allowing the Authorization header");
    const HttpResponse e = TouchJsonError(403, "quote\" and \\ and\nnewline");
    Check(e.status == 403 && e.body.find("quote\\\" and \\\\ and\\nnewline") != std::string::npos, "TouchJsonError escapes the message");
  }

  // ---- --serve-lan token requirement --------------------------------------
  {
    const std::string err = ServeLanTokenError(true, false);
    Check(!err.empty() && err.find("--serve-lan needs") != std::string::npos,
          "--serve-lan without any token source is refused with '--serve-lan needs ...'");
    Check(ServeLanTokenError(true, true).empty(), "--serve-lan with a token source is accepted");
    Check(ServeLanTokenError(false, false).empty(), "loopback-only without a token is not refused by the LAN rule");
    Check(ServeLanTokenError(false, true).empty(), "loopback-only with a token is accepted");
  }

  // ---- command blocklist --------------------------------------------------
  {
    const char* blocked[] = {"RunScript foo.py", "runscript", "RunPythonScript x", "Python", "-Python", "_Lua print(1)", "!EditScript",
                             "ScriptEditor", "LoadPlugin a.so", "PlugInInstall x", "PluginMarketplaceInstall x", "Macro foo",
                             "  RunScript x", "= 1+1", "   =os.execute('id')", "-_!Python", "RUNSCRIPT x", "\tlua"};
    for (const char* c : blocked) Check(IsBlockedTouchCommand(c), std::string("blocked: '") + c + "'");
    const char* allowed[] = {"Line 0,0,0 10,0,0", "_Box 0,0,0 1,1,1", "Circle", "Undo", "SelAll", "Delete", "-Extrude", "Layer",
                             "Scriptish", "Macros", "Pythonic", "ExportAs a.stl", "", "   ", "Line = 3"};
    for (const char* c : allowed) Check(!IsBlockedTouchCommand(c), std::string("allowed: '") + c + "'");
  }

  // ---- routing predicates -------------------------------------------------
  Check(IsTouchPath("/touch") && IsTouchPath("/touch/") && IsTouchPath("/touch/app.js") && IsTouchPath("/touch/api/ping"),
        "IsTouchPath: /touch, /touch/, files and the API");
  Check(!IsTouchPath("/touchy") && !IsTouchPath("/run") && !IsTouchPath("/") && !IsTouchPath("/x/touch/"),
        "IsTouchPath: other paths are not /touch");
  Check(IsTouchApiPath("/touch/api/ping") && IsTouchApiPath("/touch/api") && IsTouchApiPath("/touch/api/"),
        "IsTouchApiPath: the API subtree");
  Check(!IsTouchApiPath("/touch/") && !IsTouchApiPath("/touch/apiary.js") && !IsTouchApiPath("/touch/app.js") && !IsTouchApiPath("/run"),
        "IsTouchApiPath: static files and other routes are not API");

  // ---- static-file path whitelist / traversal ------------------------------
  {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec) / "dino8_touch_api_test_tree";
    fs::remove_all(base, ec);
    const fs::path web = base / "web";
    WriteFile(web / "index.html", "<!doctype html><title>t</title>");
    WriteFile(web / "app.js", "console.log(1)");
    WriteFile(web / "sub" / "deep.css", "body{}");
    WriteFile(web / "notes.txt", "not whitelisted");
    WriteFile(web / ".hidden.js", "hidden");
    WriteFile(base / "secret.js", "SECRET OUTSIDE THE WEB ROOT");
    const std::string root = web.string();
    const std::string secret_abs = (base / "secret.js").generic_string();

    auto serve = [&](const std::string& method, const std::string& path) { return ServeTouchStatic(root, Req(method, path), path); };

    HttpResponse r = serve("GET", "/touch/");
    Check(r.status == 200 && r.body.find("<title>t</title>") != std::string::npos && r.content_type.find("text/html") == 0,
          "GET /touch/ serves index.html");
    Check(r.headers.count("Content-Security-Policy") == 1 &&
              r.headers.at("Content-Security-Policy").find("frame-ancestors 'none'") != std::string::npos &&
              r.headers.at("X-Content-Type-Options") == "nosniff",
          "index.html carries the CSP and nosniff headers");
    r = serve("GET", "/touch/app.js");
    Check(r.status == 200 && r.body == "console.log(1)" && r.content_type.find("text/javascript") == 0,
          "GET /touch/app.js serves the script with a JS type");
    r = serve("GET", "/touch/sub/deep.css");
    Check(r.status == 200 && r.content_type.find("text/css") == 0, "a file in a sub-directory is served");
    r = serve("HEAD", "/touch/app.js");
    Check(r.status == 200 && r.body.empty(), "HEAD answers 200 with no body");
    r = serve("GET", "/touch");
    Check(r.status == 301 && r.headers.at("Location") == "/touch/", "/touch redirects to /touch/");

    Check(serve("POST", "/touch/app.js").status == 405, "POST to a static file is refused (405)");
    Check(serve("PUT", "/touch/app.js").status == 405, "PUT to a static file is refused (405)");
    Check(serve("DELETE", "/touch/app.js").status == 405, "DELETE of a static file is refused (405)");

    const char* traversal[] = {"/touch/../secret.js",
                               "/touch/../../secret.js",
                               "/touch/sub/../../secret.js",
                               "/touch/..%2fsecret.js",
                               "/touch/%2e%2e/secret.js",
                               "/touch/%2e%2e%2fsecret.js",
                               "/touch/..\\secret.js",
                               "/touch/sub/..\\..\\secret.js",
                               "/touch/....//secret.js",
                               "/touch/.hidden.js",
                               "/touch/sub/.hidden.js",
                               "/touch/notes.txt",
                               "/touch/missing.js",
                               "/touch/app.js%00.png",
                               "/touch/app.js?x=../../secret.js"};
    for (const char* t : traversal) {
      r = serve("GET", t);
      Check(r.status == 404 && r.body.find("SECRET") == std::string::npos, std::string("refused (404, nothing leaked): ") + t);
    }
    {
      // An absolute filesystem path smuggled after the prefix must be looked
      // up UNDER the web root (where it does not exist), never as-is.
      const std::string p = "/touch/" + secret_abs;
      r = serve("GET", p);
      Check(r.status == 404 && r.body.find("SECRET") == std::string::npos, "an absolute path after /touch/ is not followed out of the root");
    }
    r = ServeTouchStatic("", Req("GET", "/touch/app.js"), "/touch/app.js");
    Check(r.status == 404, "no web root found -> 404 (not an open file server)");
    r = ServeTouchStatic("/definitely/not/a/dir", Req("GET", "/touch/app.js"), "/touch/app.js");
    Check(r.status == 404, "web root that does not exist -> 404");

    // FindTouchWebRoot: an explicit override wins only if it is a directory.
    Check(FindTouchWebRoot("/nonexistent-exe-dir", root) == root, "FindTouchWebRoot: an existing --touch-dir override wins");
    Check(FindTouchWebRoot("/nonexistent-exe-dir", "/definitely/not/a/dir").empty(),
          "FindTouchWebRoot: a missing override yields '' (no silent fallback)");

    // Pairing token: 32 lowercase hex chars, different on each call.
    const std::string t1 = GeneratePairingToken(), t2 = GeneratePairingToken();
    bool hex = t1.size() == 32;
    for (char c : t1) hex = hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    Check(hex, "GeneratePairingToken: 32 lowercase hex characters");
    Check(t1 != t2, "GeneratePairingToken: two calls differ");

    fs::remove_all(base, ec);
  }

  std::printf("test_touch_api: %d checks, %d failure(s)\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
