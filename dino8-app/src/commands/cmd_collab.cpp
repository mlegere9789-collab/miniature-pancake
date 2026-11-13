// CollabHost / CollabJoin / CollabLeave / CollabStatus: the interactive
// surface for net/CollabSession.h's real-time document-sharing session -
// see that header for the full, honestly-scoped sync model (whole-document
// .3dm snapshot broadcast over a real TCP relay, last-committer-wins, no
// merge). PARITY_MAP.md's "Real-time multi-user collaboration / co-editing"
// (app_ecosystem) and "Real-time multi-user collaborative editing"
// (app_ux) items had no network code of any kind before this.
#include <cctype>
#include <cstdlib>

#include "commands/annotate_common.h"
#include "commands/cmd_common.h"
#include "net/CollabSession.h"

namespace dino8::app {

namespace {

void CollabHostCmd(CommandContext& ctx) {
  const auto opts = TakeOptionTokens(ctx);
  const std::string port_str = OptionOr(opts, "port");
  const int port = port_str.empty() ? 0 : std::atoi(port_str.c_str());
  CollabOptions options;
  options.token = OptionOr(opts, "token");
  std::string lan = OptionOr(opts, "lan");
  for (char& c : lan) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  options.lan = lan == "yes" || lan == "true" || lan == "1" || lan == "on";
  std::string error;
  if (!CollabSession::Get().Host(port, options, error)) {
    ctx.Warn("CollabHost: " + error);
    return;
  }
  CollabSession& s = CollabSession::Get();
  const int bound = s.HostPort();
  const std::string tok = options.token.empty() ? "" : " Token=<the token>";
  ctx.Print("CollabHost: hosting on " + s.BoundAddress() + ":" + std::to_string(bound) + (s.Lan() ? " (LAN: every interface)" : "") +
            (s.TokenRequired() ? ", token required" : "") + " - a collaborator on this machine runs CollabJoin Port=" +
            std::to_string(bound) + tok + (s.Lan() ? "; on another machine, CollabJoin Host=<this machine's IP> Port=" + std::to_string(bound) + tok
                                                    : "; to reach it from another machine host with Lan=Yes Token=<at least 8 characters>"));
}

void CollabJoinCmd(CommandContext& ctx) {
  const auto opts = TakeOptionTokens(ctx);
  const std::string host = OptionOr(opts, "host", "127.0.0.1");
  const std::string port_str = OptionOr(opts, "port");
  if (port_str.empty()) {
    ctx.Warn("CollabJoin Host=<address, default 127.0.0.1> Port=<port>");
    return;
  }
  CollabOptions options;
  options.token = OptionOr(opts, "token");
  std::string error;
  if (!CollabSession::Get().Join(host, std::atoi(port_str.c_str()), options, error)) {
    ctx.Warn("CollabJoin: " + error);
    return;
  }
  ctx.Print("CollabJoin: connected to " + host + ":" + port_str + (options.token.empty() ? "" : " (authenticating with the token)") +
            " - edits now sync automatically, once per frame");
}

void CollabLeaveCmd(CommandContext& ctx) {
  if (!CollabSession::Get().IsActive()) {
    ctx.Print("CollabLeave: no active session");
    return;
  }
  CollabSession::Get().Leave();
  ctx.Print("CollabLeave: left the session");
}

void CollabStatusCmd(CommandContext& ctx) {
  CollabSession& s = CollabSession::Get();
  if (!s.IsActive()) {
    ctx.Print("CollabStatus: no active session (CollabHost or CollabJoin to start one)" +
              (s.LastError().empty() ? std::string() : "; the last session ended: " + s.LastError()));
    return;
  }
  const char* role = s.Role() == CollabRole::Host ? "Host" : "Joined";
  std::string line = std::string("CollabStatus: ") + role + ", " + std::to_string(s.PeerCount()) + " peer(s), " +
                     std::to_string(s.SentCount()) + " snapshot(s) sent, " + std::to_string(s.AppliedCount()) + " applied this session";
  if (s.TokenRequired()) line += ", token required, " + std::to_string(s.RejectedCount()) + " connection(s) rejected";
  if (s.Role() == CollabRole::Joined && !s.Authenticated()) line += ", waiting for the host to accept the token";
  ctx.Print(line);
}

}  // namespace

void RegisterCollabCommands(CommandEngine& e) {
  Reg(e, "CollabHost", Immediate(CollabHostCmd), CommandStatus::Implemented,
      "Starts a real-time collaboration session hosted by this instance and joins it over loopback. "
      "Port=<port, 0 or omitted = OS-assigned ephemeral port>. Token=<at least 8 characters> makes every collaborator authenticate "
      "(HMAC-SHA256 challenge/response; the token is never sent, but the document traffic is not encrypted); Lan=Yes listens on every "
      "interface instead of loopback only and requires a Token. See net/CollabSession.h for the exact sync model "
      "(whole-document snapshot broadcast, last-committer-wins - not a merge).");
  Reg(e, "CollabJoin", Immediate(CollabJoinCmd), CommandStatus::Implemented,
      "Joins a collaboration session already started elsewhere with CollabHost. "
      "Host=<numeric IPv4 address, default 127.0.0.1> Port=<port> [Token=<the host's token>].");
  Reg(e, "CollabLeave", Immediate(CollabLeaveCmd), CommandStatus::Implemented,
      "Leaves the current collaboration session, if any (and stops hosting it, if this instance was the host).");
  Reg(e, "CollabStatus", Immediate(CollabStatusCmd), CommandStatus::Implemented,
      "Reports the current collaboration session's role, connected peer count, and snapshots sent/applied so far.");
}

}  // namespace dino8::app
