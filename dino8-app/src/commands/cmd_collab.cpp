// CollabHost / CollabJoin / CollabLeave / CollabStatus: the interactive
// surface for net/CollabSession.h's real-time document-sharing session -
// see that header for the full, honestly-scoped sync model (whole-document
// .3dm snapshot broadcast over a real TCP relay, last-committer-wins, no
// merge). PARITY_MAP.md's "Real-time multi-user collaboration / co-editing"
// (app_ecosystem) and "Real-time multi-user collaborative editing"
// (app_ux) items had no network code of any kind before this.
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
  std::string error;
  if (!CollabSession::Get().Host(port, error)) {
    ctx.Warn("CollabHost: " + error);
    return;
  }
  const int bound = CollabSession::Get().HostPort();
  ctx.Print("CollabHost: hosting on 127.0.0.1:" + std::to_string(bound) +
            " - a collaborator on this machine runs CollabJoin Port=" + std::to_string(bound) +
            "; on another machine, CollabJoin Host=<this machine's IP> Port=" + std::to_string(bound));
}

void CollabJoinCmd(CommandContext& ctx) {
  const auto opts = TakeOptionTokens(ctx);
  const std::string host = OptionOr(opts, "host", "127.0.0.1");
  const std::string port_str = OptionOr(opts, "port");
  if (port_str.empty()) {
    ctx.Warn("CollabJoin Host=<address, default 127.0.0.1> Port=<port>");
    return;
  }
  std::string error;
  if (!CollabSession::Get().Join(host, std::atoi(port_str.c_str()), error)) {
    ctx.Warn("CollabJoin: " + error);
    return;
  }
  ctx.Print("CollabJoin: connected to " + host + ":" + port_str + " - edits now sync automatically, once per frame");
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
    ctx.Print("CollabStatus: no active session (CollabHost or CollabJoin to start one)");
    return;
  }
  const char* role = s.Role() == CollabRole::Host ? "Host" : "Joined";
  ctx.Print(std::string("CollabStatus: ") + role + ", " + std::to_string(s.PeerCount()) + " peer(s), " +
            std::to_string(s.SentCount()) + " snapshot(s) sent, " + std::to_string(s.AppliedCount()) +
            " applied this session");
}

}  // namespace

void RegisterCollabCommands(CommandEngine& e) {
  Reg(e, "CollabHost", Immediate(CollabHostCmd), CommandStatus::Implemented,
      "Starts a real-time collaboration session hosted by this instance and joins it over loopback. "
      "Port=<port, 0 or omitted = OS-assigned ephemeral port>. See net/CollabSession.h for the exact sync model "
      "(whole-document snapshot broadcast, last-committer-wins - not a merge).");
  Reg(e, "CollabJoin", Immediate(CollabJoinCmd), CommandStatus::Implemented,
      "Joins a collaboration session already started elsewhere with CollabHost. "
      "Host=<numeric IPv4 address, default 127.0.0.1> Port=<port>.");
  Reg(e, "CollabLeave", Immediate(CollabLeaveCmd), CommandStatus::Implemented,
      "Leaves the current collaboration session, if any (and stops hosting it, if this instance was the host).");
  Reg(e, "CollabStatus", Immediate(CollabStatusCmd), CommandStatus::Implemented,
      "Reports the current collaboration session's role, connected peer count, and snapshots sent/applied so far.");
}

}  // namespace dino8::app
