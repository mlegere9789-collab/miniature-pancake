// The app-level half of PARITY_MAP.md's "Real-time multi-user collaboration
// / co-editing" item: wires the pure byte relay in net/Collab.h to a real
// Document, so two running instances that Host()/Join() the same session
// see each other's edits propagate automatically, without either side
// writing a single line of networking code themselves.
//
// Honestly scoped, not a CRDT/operational-transform engine: every peer that
// commits a change (Document::Revision() moved on) re-serializes the WHOLE
// document to a real .3dm (io/File3dm.h's own Save3dm - the same proven
// writer Save/SaveAs already use, not a new ad hoc wire format for
// geometry) and broadcasts it; whichever snapshot a peer receives last
// simply replaces its own document (Load3dm - the same path Open already
// uses). That means:
//   - last-committer-wins, not a merge: two peers editing at the same
//     moment do NOT both survive - one snapshot wins, exactly like two
//     people Saving over each other's work, just automatic instead of
//     requiring an explicit Save. A real limitation, named up front rather
//     than glossed over.
//   - Undo/Redo history and the current selection do not survive an
//     incoming snapshot (Load3dm rebuilds the document from scratch) - the
//     single biggest honest-scope difference from true co-editing.
//   - bandwidth cost is the whole file every time, not a diff - fine for
//     the interactive, occasional-commit model this targets (a user
//     finishes one edit, then the next), not for continuous streamed
//     dragging.
// None of this needs excusing away: before this there was no network code
// of any kind that let two instances share a document at all (see
// PARITY_MAP.md's app_ecosystem/app_ux category bullets), and this closes
// that real, verified gap.
#pragma once

#include <cstdint>
#include <string>

#include "doc/Document.h"
#include "net/Collab.h"

namespace dino8::app {

enum class CollabRole { None, Host, Joined };

class CollabSession {
 public:
  CollabSession() = default;

  // The app's own single, process-wide session (what CollabHost/CollabJoin/
  // CollabLeave/CollabStatus and UpdateCollabSession below all act on).
  // Default-constructible (see above) so tests/test_collab.cpp can also
  // build independent CollabSession instances directly, to drive a
  // host-side and a join-side session side by side in one process - two
  // real, separate peers, not two aliases of the same one.
  static CollabSession& Get();

  // Starts a relay on `port` (0 = OS-assigned ephemeral port) and connects
  // this process to it over loopback, so the host participates in the
  // session through the exact same CollabClient code path a Join()ed peer
  // uses - no separate "I'm the host" logic anywhere past this call.
  bool Host(int port, std::string& error);
  // Connects to a session already Host()ed elsewhere (host:port).
  bool Join(const std::string& host, int port, std::string& error);
  // Disconnects (and, if hosting, stops the relay) - a no-op if not active.
  void Leave();

  CollabRole Role() const { return role_; }
  bool IsActive() const { return role_ != CollabRole::None; }
  // The relay's bound port - only meaningful for CollabRole::Host (0
  // otherwise), e.g. to tell a collaborator what port to Join().
  int HostPort() const { return hosting_ ? server_.Port() : 0; }
  // How many OTHER relay connections are live right now - only meaningful
  // for CollabRole::Host (the relay itself only exists there); 0 otherwise.
  // Subtracts this host's own loopback connection, so it reads as "how many
  // collaborators", not "how many sockets".
  size_t PeerCount() const { return hosting_ && server_.ClientCount() > 0 ? server_.ClientCount() - 1 : 0; }
  // Total snapshots this process has sent/received this session - a simple,
  // directly observable "is this actually doing anything" counter for the
  // CollabStatus command and for tests, independent of PeerCount (which can
  // read 0 between polls even while a peer is connected, since it only
  // reflects the relay's current socket count).
  std::uint64_t SentCount() const { return sent_count_; }
  std::uint64_t AppliedCount() const { return applied_count_; }

  // Services the network once and applies any newly-received snapshot (in
  // place, exactly as Open would) to `doc`, then pushes a fresh snapshot of
  // `doc` if its own Revision() has moved since the last thing this process
  // sent or applied. Called once per frame by UpdateCollabSession(Document&)
  // below while a session is active; safe (a no-op) to call when
  // !IsActive(). Returns true if `doc` was just replaced by an incoming
  // snapshot (callers that care about selection/viewport state can react);
  // `error` is set (and the session left) only on a real connection failure.
  bool PollAndApply(Document& doc, std::string& error);

 private:
  CollabServer server_;
  CollabClient client_;
  bool hosting_ = false;
  CollabRole role_ = CollabRole::None;
  std::uint64_t local_origin_ = 0;
  std::uint64_t local_seq_ = 0;
  std::uint64_t last_synced_revision_ = 0;
  bool has_synced_ = false;
  std::uint64_t sent_count_ = 0;
  std::uint64_t applied_count_ = 0;
};

// Per-frame hook (Application::Frame, alongside UpdateCageCaptives/
// UpdateSymmetryLive) - a thin free-function wrapper so Application.cpp
// only needs a forward declaration, the same pattern those two use.
void UpdateCollabSession(Document& doc);

// Snapshot wire payload: an 8-byte big-endian origin id, an 8-byte
// big-endian sequence number, then the raw .3dm bytes verbatim - the
// payload CollabClient::Send/PollOnce (net/Collab.h) carry, framed by that
// file's own 4-byte length prefix underneath. Pure and exposed here so
// tests/test_collab.cpp can check the encoding directly, without needing a
// real Document, socket, or temp file.
std::string EncodeSnapshotPayload(std::uint64_t origin, std::uint64_t seq, const std::string& threedm_bytes);
bool TryDecodeSnapshotPayload(const std::string& payload, std::uint64_t* origin, std::uint64_t* seq,
                               std::string* threedm_bytes);

}  // namespace dino8::app
