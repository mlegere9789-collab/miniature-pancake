// The Application-free half of the Dino 8 Touch server side (see
// net/TouchApi.h for the routes): request gating and the static-file server.
// Kept free of Application/Document/CommandEngine so tests/test_touch_api.cpp
// can link the real code and test it without a window.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "net/ComputeServer.h"

namespace dino8::app {

// JSON string escape / the CORS header set / the {"ok":false,"error":...}
// body every /touch/api/ refusal uses.
std::string TouchEscape(const std::string& s);
void TouchCorsHeaders(HttpResponse& r);
HttpResponse TouchJsonError(int status, const std::string& msg);

// Compares two strings in time independent of where they first differ, so
// the token check does not leak the token one byte at a time to another
// local process timing repeated guesses against the port.
bool ConstantTimeEquals(const std::string& a, const std::string& b);

// True when `req` carries "Authorization: Bearer X" and X equals (or, for a
// "sha256:HEX" candidate, hashes to) the token of one of `candidates`
// ({name, token} pairs); `caller_name` is then that entry's name.
bool MatchBearerToken(const HttpRequest& req, const std::vector<std::pair<std::string, std::string>>& candidates,
                      std::string& caller_name);

// The 401 answer for a /touch/api/ request with a missing or wrong token, and
// the 403 answer when the server has no token configured at all (the touch
// API never runs open).
HttpResponse TouchUnauthorizedResponse();
HttpResponse TouchNoTokenResponse();

// "" if the --serve-lan request is acceptable, otherwise the error line main()
// prints before exiting 1: binding beyond loopback needs some token source.
std::string ServeLanTokenError(bool serve_lan, bool have_token_source);

// Commands the touch channel refuses (script/plug-in loaders and the "="
// evaluator). Case-insensitive, ignores a leading '-', '_' or '!'.
bool IsBlockedTouchCommand(const std::string& line);

bool IsTouchPath(const std::string& path);     // "/touch" or "/touch/..."
bool IsTouchApiPath(const std::string& path);  // "/touch/api/..."

// The directory holding the PWA files, or "" if none was found.
std::string FindTouchWebRoot(const std::string& exe_dir, const std::string& override_dir);

// Serves a static PWA file (GET/HEAD) from `web_root`: fixed extension
// whitelist, plain file-name characters only, no path traversal.
HttpResponse ServeTouchStatic(const std::string& web_root, const HttpRequest& req, const std::string& path);

// CORS preflight answer for a /touch/api/ route.
HttpResponse TouchPreflight();

// Random pairing token (32 hex chars from the OS entropy source).
std::string GeneratePairingToken();

}  // namespace dino8::app
