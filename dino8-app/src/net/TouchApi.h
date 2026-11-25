// Server side of the "Dino 8 Touch" companion app (touch/web/, a PWA for
// tablets and phones). Two parts, both mounted on the existing compute
// server (net/ComputeServer.h) by main.cpp's compute_handler:
//
//   * Static PWA assets under /touch/ - the app shell (HTML/CSS/JS, the web
//     manifest, the service worker, icons). These are code, not data, so
//     they are served without a token (a browser cannot attach a bearer
//     header to a page navigation); GET/HEAD only, a fixed extension
//     whitelist, no path traversal.
//   * A small JSON API under /touch/api/ that always requires the pairing
//     token (the same --serve-token / --serve-token-file bearer tokens the
//     rest of the compute server uses - see main.cpp). If the server was
//     started with no token at all, the API refuses with 403 instead of
//     being open.
//
// Routes (all JSON):
//   GET  /touch/api/ping      {ok, app, version, objects}
//   GET  /touch/api/state     object/selection counts, undo state, the
//                             active command's prompt, the layer list
//   GET  /touch/api/scene     every visible object's tessellated triangles /
//                             edges / curve segments / points, effective
//                             colour, selection flag (compact flat arrays)
//   POST /touch/api/command   run command line(s) (body: JSON {"line"|"lines"}
//                             or plain text); returns the new history lines
//   POST /touch/api/select    {"ids":[...],"mode":"set|add|toggle|clear"}
//   POST /touch/api/layer     {"index":N,"visible":bool}
// plus OPTIONS (CORS preflight) on every /touch/api/ route, because an
// installed PWA served from one origin may talk to a desktop on another.
#pragma once

#include <string>

#include "net/ComputeServer.h"
#include "net/TouchGate.h"  // gating + static files (Application-free; unit-tested)

namespace dino8::app {

class Application;

// Handles an already-authenticated /touch/api/ request.
HttpResponse HandleTouchApi(Application& app, const HttpRequest& req, const std::string& path, const std::string& caller);

}  // namespace dino8::app
