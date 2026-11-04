#include "net/TouchGate.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

#include "util/Sha256.h"

// The Application-free half of the Dino 8 Touch server side: everything that
// decides *whether* a request may reach the app (bearer-token matching, the
// --serve-lan token requirement, the command blocklist, the /touch/ static
// path whitelist). Split out of TouchApi.cpp so tests/test_touch_api.cpp can
// link and exercise the real code without a window, a Document or a
// CommandEngine.

namespace dino8::app {

std::string TouchEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += static_cast<char>(c);
    }
  }
  return out;
}


void TouchCorsHeaders(HttpResponse& r) {
  // The API is bearer-token authenticated (no cookies, no ambient
  // credentials), so a wildcard origin does not widen who can call it: a
  // page on any origin still needs the pairing token to get past the gate.
  r.headers["Access-Control-Allow-Origin"] = "*";
  r.headers["Access-Control-Allow-Headers"] = "Authorization, Content-Type";
  r.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
  r.headers["Access-Control-Max-Age"] = "600";
  r.headers["Cache-Control"] = "no-store";
}

HttpResponse TouchJsonError(int status, const std::string& msg) {
  HttpResponse r;
  r.status = status;
  r.content_type = "application/json";
  r.body = "{\"ok\":false,\"error\":\"" + TouchEscape(msg) + "\"}\n";
  TouchCorsHeaders(r);
  return r;
}


bool ConstantTimeEquals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  return diff == 0;
}

bool MatchBearerToken(const HttpRequest& req, const std::vector<std::pair<std::string, std::string>>& candidates,
                      std::string& caller_name) {
  const auto it = req.headers.find("authorization");
  if (it == req.headers.end() || it->second.rfind("Bearer ", 0) != 0) return false;
  const std::string presented = it->second.substr(7);
  for (const auto& [name, token] : candidates) {
    // A "sha256:HEX" candidate (only meaningful from a --serve-token-file
    // line) never stores the real bearer secret: the file holds only its
    // SHA-256 digest, so a leaked token FILE leaks nothing a caller could
    // present as the token itself. Comparing two fixed-length hex digests
    // with ConstantTimeEquals is as safe as comparing a plaintext candidate.
    const bool is_hash = token.rfind("sha256:", 0) == 0;
    const bool ok = is_hash ? ConstantTimeEquals(dino8::util::Sha256Hex(presented), token.substr(7))
                             : ConstantTimeEquals(presented, token);
    if (ok) {
      caller_name = name;
      return true;
    }
  }
  return false;
}

HttpResponse TouchUnauthorizedResponse() {
  HttpResponse resp;
  resp.status = 401;
  resp.content_type = "application/json";
  resp.body = "{\"ok\":false,\"error\":\"missing or incorrect pairing token\"}\n";
  resp.headers["Access-Control-Allow-Origin"] = "*";
  resp.headers["Cache-Control"] = "no-store";
  return resp;
}

HttpResponse TouchNoTokenResponse() {
  HttpResponse resp;
  resp.status = 403;
  resp.content_type = "application/json";
  resp.body = "{\"ok\":false,\"error\":\"the touch API needs a pairing token (start with --serve-touch or --serve-token)\"}\n";
  resp.headers["Access-Control-Allow-Origin"] = "*";
  return resp;
}

std::string ServeLanTokenError(bool serve_lan, bool have_token_source) {
  if (serve_lan && !have_token_source) {
    return "serve: --serve-lan needs --serve-token, --serve-token-file or --serve-touch (refusing to expose an open server to the network)";
  }
  return "";
}

static const char* MimeFor(const std::string& ext) {
  if (ext == ".html") return "text/html; charset=utf-8";
  if (ext == ".js") return "text/javascript; charset=utf-8";
  if (ext == ".css") return "text/css; charset=utf-8";
  if (ext == ".webmanifest") return "application/manifest+json";
  if (ext == ".json") return "application/json";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".png") return "image/png";
  if (ext == ".ico") return "image/x-icon";
  return nullptr;
}

// Commands that run arbitrary script source or load code. The touch channel
// is a command *line*, not a scripting channel: a tablet that needs a script
// run asks the desktop user.
bool IsBlockedTouchCommand(const std::string& line) {
  size_t i = 0;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
  if (i < line.size() && line[i] == '=') return true;  // "= <lua>" evaluator
  while (i < line.size() && (line[i] == '-' || line[i] == '_' || line[i] == '!')) ++i;
  size_t j = i;
  while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
  std::string name = line.substr(i, j - i);
  for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  static const char* kBlocked[] = {"runscript", "runpythonscript", "python", "lua", "editscript", "scripteditor",
                                   "loadplugin", "plugininstall", "pluginmarketplaceinstall", "macro"};
  for (const char* b : kBlocked) if (name == b) return true;
  return false;
}

std::string GeneratePairingToken() {
  std::random_device rd;
  std::string t;
  char buf[16];
  for (int i = 0; i < 4; ++i) {
    std::snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(rd()));
    t += buf;
  }
  return t;
}

bool IsTouchPath(const std::string& path) { return path == "/touch" || path.rfind("/touch/", 0) == 0; }
bool IsTouchApiPath(const std::string& path) { return path.rfind("/touch/api/", 0) == 0 || path == "/touch/api"; }

std::string FindTouchWebRoot(const std::string& exe_dir, const std::string& override_dir) {
  std::error_code ec;
  if (!override_dir.empty()) return std::filesystem::is_directory(override_dir, ec) ? override_dir : std::string();
  const std::vector<std::string> candidates = {
      exe_dir + "/data/touch",
      exe_dir + "/../Resources/data/touch",       // macOS bundle
      exe_dir + "/../share/dino8/data/touch",     // Linux install
      exe_dir + "/../../data/touch",              // build tree
      exe_dir + "/../../../dino8-app/touch/web",  // straight from source
      "touch/web",
  };
  for (const std::string& p : candidates) {
    if (std::filesystem::exists(p + "/index.html", ec)) return p;
  }
  return "";
}

HttpResponse TouchPreflight() {
  HttpResponse r;
  r.status = 204;
  TouchCorsHeaders(r);
  return r;
}

HttpResponse ServeTouchStatic(const std::string& web_root, const HttpRequest& req, const std::string& path) {
  HttpResponse r;
  if (req.method != "GET" && req.method != "HEAD") {
    r.status = 405;
    r.body = "Dino 8 Touch: static files take GET\n";
    return r;
  }
  if (path == "/touch") {
    r.status = 301;
    r.headers["Location"] = "/touch/";
    return r;
  }
  if (web_root.empty()) {
    r.status = 404;
    r.body = "Dino 8 Touch: the PWA files were not found on this build (use --touch-dir DIR)\n";
    return r;
  }
  std::string rel = path.substr(std::string("/touch/").size());
  if (rel.empty()) rel = "index.html";
  // Whitelist of plain file-name characters; no '..', no hidden files.
  bool bad = rel.find("..") != std::string::npos || rel[0] == '.' || rel.size() > 128;
  for (unsigned char c : rel) {
    if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '/')) bad = true;
  }
  const std::string ext = std::filesystem::path(rel).extension().string();
  const char* mime = MimeFor(ext);
  if (bad || !mime) {
    r.status = 404;
    r.body = "Dino 8 Touch: not found\n";
    return r;
  }
  std::ifstream f(web_root + "/" + rel, std::ios::binary);
  if (!f) {
    r.status = 404;
    r.body = "Dino 8 Touch: not found\n";
    return r;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  r.content_type = mime;
  if (req.method == "GET") r.body = ss.str();
  // The service worker file must always be revalidated so an app update is
  // noticed; the browser's HTTP cache never holds the app shell stale.
  r.headers["Cache-Control"] = "no-cache";
  r.headers["X-Content-Type-Options"] = "nosniff";
  if (rel == "sw.js") r.headers["Service-Worker-Allowed"] = "/touch/";
  // The app is a closed set of same-origin files; it may only *connect* out
  // (to the paired desktop, whatever host:port the user typed).
  if (ext == ".html") {
    r.headers["Content-Security-Policy"] =
        "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; connect-src *; "
        "base-uri 'none'; form-action 'none'; frame-ancestors 'none'";
    r.headers["Referrer-Policy"] = "no-referrer";
  }
  return r;
}

}  // namespace dino8::app
