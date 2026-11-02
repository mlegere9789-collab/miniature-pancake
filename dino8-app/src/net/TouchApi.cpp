#include "net/TouchApi.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

#include "app/Application.h"
#include "commands/CommandEngine.h"
#include "doc/Document.h"
#include "util/json_mini.h"

#ifndef DINO8_VERSION
#define DINO8_VERSION "dev"
#endif

namespace dino8::app {

namespace {

std::string Esc(const std::string& s) {
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

void CorsHeaders(HttpResponse& r) {
  // The API is bearer-token authenticated (no cookies, no ambient
  // credentials), so a wildcard origin does not widen who can call it: a
  // page on any origin still needs the pairing token to get past the gate.
  r.headers["Access-Control-Allow-Origin"] = "*";
  r.headers["Access-Control-Allow-Headers"] = "Authorization, Content-Type";
  r.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
  r.headers["Access-Control-Max-Age"] = "600";
  r.headers["Cache-Control"] = "no-store";
}

HttpResponse JsonError(int status, const std::string& msg) {
  HttpResponse r;
  r.status = status;
  r.content_type = "application/json";
  r.body = "{\"ok\":false,\"error\":\"" + Esc(msg) + "\"}\n";
  CorsHeaders(r);
  return r;
}

// Writes the first `take` floats of every `stride`-float vertex record as a
// flat comma-separated list (no brackets), 5 significant digits.
void AppendFloats(std::string& out, const std::vector<float>& v, size_t stride, size_t take) {
  char buf[32];
  bool first = true;
  for (size_t i = 0; i + stride <= v.size(); i += stride) {
    for (size_t k = 0; k < take; ++k) {
      if (!first) out += ',';
      first = false;
      std::snprintf(buf, sizeof(buf), "%.5g", static_cast<double>(v[i + k]));
      out += buf;
    }
  }
}

const char* MimeFor(const std::string& ext) {
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

HttpResponse Ok(const std::string& body) {
  HttpResponse r;
  r.content_type = "application/json";
  r.body = body;
  CorsHeaders(r);
  return r;
}

std::string LayersJson(const Document& doc) {
  std::string s = "[";
  for (size_t i = 0; i < doc.Layers().size(); ++i) {
    const Layer& L = doc.Layers()[i];
    if (i) s += ',';
    char col[64];
    std::snprintf(col, sizeof(col), "[%.3f,%.3f,%.3f]", static_cast<double>(L.color.r), static_cast<double>(L.color.g),
                  static_cast<double>(L.color.b));
    s += "{\"index\":" + std::to_string(i) + ",\"name\":\"" + Esc(doc.LayerFullPath(static_cast<int>(i))) +
         "\",\"visible\":" + (L.visible ? "true" : "false") + ",\"locked\":" + (L.locked ? "true" : "false") +
         ",\"color\":" + col + "}";
  }
  return s + "]";
}

std::string SelectionJson(const Document& doc) {
  std::string s = "[";
  bool first = true;
  for (ObjectId id : doc.SelectedIds()) {
    if (!first) s += ',';
    first = false;
    s += std::to_string(id);
  }
  return s + "]";
}

std::string StateJson(Application& app) {
  Document& doc = app.Doc();
  CommandEngine& eng = app.Engine();
  return "{\"ok\":true,\"objects\":" + std::to_string(doc.ObjectCount()) + ",\"selected\":" + SelectionJson(doc) +
         ",\"can_undo\":" + (doc.CanUndo() ? "true" : "false") + ",\"can_redo\":" + (doc.CanRedo() ? "true" : "false") +
         ",\"active\":\"" + Esc(eng.IsRunning() ? eng.ActiveName() : "") + "\",\"prompt\":\"" +
         Esc(eng.IsRunning() ? eng.Prompt() : "") + "\",\"layers\":" + LayersJson(doc) + "}\n";
}

HttpResponse SceneJson(Application& app) {
  Document& doc = app.Doc();
  const double tol = doc.Settings().absolute_tolerance > 0 ? doc.Settings().absolute_tolerance : 0.001;
  std::string body = "{\"ok\":true,\"objects\":[";
  bool first = true;
  for (const SceneObject& o : doc.Objects()) {
    if (!doc.IsObjectVisible(o)) continue;
    o.EnsureDisplay(tol, 0.05);
    const DisplayCache& d = o.Display();
    if (!first) body += ',';
    first = false;
    const Color c = doc.EffectiveColor(o);
    char col[64];
    std::snprintf(col, sizeof(col), "[%.3f,%.3f,%.3f]", static_cast<double>(c.r), static_cast<double>(c.g), static_cast<double>(c.b));
    body += "{\"id\":" + std::to_string(o.id) + ",\"type\":\"" + Esc(ObjectKindName(o.kind)) + "\",\"name\":\"" +
            Esc(o.name) + "\",\"color\":" + col + ",\"selected\":" + (o.selected ? "true" : "false") +
            ",\"locked\":" + (o.locked ? "true" : "false") + ",\"tri\":[";
    AppendFloats(body, d.triangles, 6, 3);
    body += "],\"edges\":[";
    AppendFloats(body, d.edges, 3, 3);
    body += "],\"lines\":[";
    AppendFloats(body, d.lines, 3, 3);
    body += "],\"points\":[";
    AppendFloats(body, d.points, 3, 3);
    body += "]}";
  }
  body += "]}\n";
  return Ok(body);
}

HttpResponse RunCommands(Application& app, const HttpRequest& req) {
  std::vector<std::string> lines;
  const std::string& b = req.body;
  size_t k = 0;
  while (k < b.size() && std::isspace(static_cast<unsigned char>(b[k]))) ++k;
  if (k < b.size() && b[k] == '{') {
    json::Value root;
    std::string err;
    if (!json::Parse(b, root, err) || !root.IsObject()) return JsonError(400, "malformed JSON body: " + err);
    if (root["line"].IsString()) lines.push_back(root["line"].string);
    if (root["lines"].IsArray()) {
      for (size_t i = 0; i < root["lines"].Size(); ++i) {
        if (!root["lines"][i].IsString()) return JsonError(400, "\"lines\" must be an array of strings");
        lines.push_back(root["lines"][i].string);
      }
    }
  } else {
    std::istringstream in(b);
    std::string l;
    while (std::getline(in, l)) lines.push_back(l);
  }
  if (lines.empty()) return JsonError(400, "no command line given (body: {\"line\":\"...\"}, {\"lines\":[...]} or plain text)");
  if (lines.size() > 64) return JsonError(400, "too many command lines in one request (max 64)");
  for (std::string& l : lines) {
    while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
    if (l.size() > 4096) return JsonError(400, "command line too long");
    if (IsBlockedTouchCommand(l)) return JsonError(403, "command not available over the touch channel: " + l.substr(0, 40));
  }

  CommandEngine& eng = app.Engine();
  const size_t before = eng.History().size();
  const size_t objects_before = app.Doc().ObjectCount();
  for (const std::string& l : lines) eng.Execute(l);
  const auto& hist = eng.History();
  // History is a capped deque: if it wrapped, the new lines are the tail.
  const size_t start = hist.size() >= before ? before : 0;
  std::string out = "[";
  bool first = true;
  for (size_t i = start; i < hist.size(); ++i) {
    if (!first) out += ',';
    first = false;
    out += "\"" + Esc(hist[i]) + "\"";
  }
  out += "]";
  Document& doc = app.Doc();
  return Ok("{\"ok\":true,\"output\":" + out + ",\"objects\":" + std::to_string(doc.ObjectCount()) +
            ",\"objects_before\":" + std::to_string(objects_before) + ",\"selected\":" + SelectionJson(doc) +
            ",\"active\":\"" + Esc(eng.IsRunning() ? eng.ActiveName() : "") + "\",\"prompt\":\"" +
            Esc(eng.IsRunning() ? eng.Prompt() : "") + "\"}\n");
}

HttpResponse SelectIds(Application& app, const HttpRequest& req) {
  json::Value root;
  std::string err;
  if (!json::Parse(req.body, root, err) || !root.IsObject()) return JsonError(400, "malformed JSON body: " + err);
  const std::string mode = root["mode"].IsString() ? root["mode"].string : "set";
  if (mode != "set" && mode != "add" && mode != "toggle" && mode != "clear") return JsonError(400, "mode must be set, add, toggle or clear");
  Document& doc = app.Doc();
  std::vector<ObjectId> ids;
  if (root["ids"].IsArray()) {
    for (size_t i = 0; i < root["ids"].Size(); ++i) {
      if (root["ids"][i].type != json::Value::Type::Number) return JsonError(400, "ids must be numbers");
      ids.push_back(static_cast<ObjectId>(root["ids"][i].number));
    }
  }
  if (mode == "set" || mode == "clear") doc.SelectNone();
  if (mode != "clear") {
    for (ObjectId id : ids) {
      const SceneObject* o = doc.Find(id);
      if (!o || !doc.IsObjectVisible(*o)) continue;
      doc.Select(id, mode == "toggle" ? !o->selected : true);
    }
  }
  return Ok("{\"ok\":true,\"selected\":" + SelectionJson(doc) + "}\n");
}

HttpResponse SetLayer(Application& app, const HttpRequest& req) {
  json::Value root;
  std::string err;
  if (!json::Parse(req.body, root, err) || !root.IsObject()) return JsonError(400, "malformed JSON body: " + err);
  Document& doc = app.Doc();
  if (root["index"].type != json::Value::Type::Number || root["visible"].type != json::Value::Type::Bool) {
    return JsonError(400, "expected {\"index\":N,\"visible\":bool}");
  }
  const int idx = static_cast<int>(root["index"].number);
  if (idx < 0 || idx >= static_cast<int>(doc.Layers().size())) return JsonError(400, "layer index out of range");
  doc.Layers()[static_cast<size_t>(idx)].visible = root["visible"].boolean;
  return Ok("{\"ok\":true,\"layers\":" + LayersJson(doc) + "}\n");
}

}  // namespace

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
  CorsHeaders(r);
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

HttpResponse HandleTouchApi(Application& app, const HttpRequest& req, const std::string& path, const std::string& caller) {
  (void)caller;
  const size_t prefix = std::string("/touch/api/").size();
  const std::string route = path.size() > prefix ? path.substr(prefix) : "";
  if (route == "ping" && req.method == "GET") {
    return Ok(std::string("{\"ok\":true,\"app\":\"Dino 8\",\"version\":\"") + DINO8_VERSION + "\",\"objects\":" +
              std::to_string(app.Doc().ObjectCount()) + "}\n");
  }
  if (route == "state" && req.method == "GET") return Ok(StateJson(app));
  if (route == "scene" && req.method == "GET") return SceneJson(app);
  if (route == "command" && req.method == "POST") return RunCommands(app, req);
  if (route == "select" && req.method == "POST") return SelectIds(app, req);
  if (route == "layer" && req.method == "POST") return SetLayer(app, req);
  if (route == "ping" || route == "state" || route == "scene" || route == "command" || route == "select" || route == "layer") {
    return JsonError(405, "wrong method for /touch/api/" + route);
  }
  return JsonError(404, "unknown touch API route (ping, state, scene, command, select, layer)");
}

}  // namespace dino8::app
