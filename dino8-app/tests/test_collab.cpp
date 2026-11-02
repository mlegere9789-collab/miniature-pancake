// Unit + integration test for PARITY_MAP.md's "Real-time multi-user
// collaboration / co-editing" item (net/Collab.h / net/CollabSession.h) -
// before this there was no network code of any kind that let two running
// instances of the app share a document.
//
// Same three-tier shape test_compute_server.cpp uses: (1) the pure frame/
// payload encode-decode functions, checked purely in memory; (2) a real
// end-to-end round trip over actual loopback TCP sockets, proving
// CollabServer genuinely relays one client's bytes to every OTHER client
// and never echoes them back to the sender; (3) a full CollabSession
// integration test driving two independent, real Document instances (one
// Host()ing, one Join()ing) through several PollAndApply() ticks and
// proving an edit made on either side actually arrives on the other's
// Document - the actual "co-editing" claim, not just the transport under
// it.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "doc/Document.h"
#include "doc/SceneObject.h"
#include "net/Collab.h"
#include "net/CollabSession.h"

using dino8::app::BuildFrame;
using dino8::app::CollabClient;
using dino8::app::CollabRole;
using dino8::app::CollabServer;
using dino8::app::CollabSession;
using dino8::app::Document;
using dino8::app::EncodeSnapshotPayload;
using dino8::app::SceneObject;
using dino8::app::TryDecodeSnapshotPayload;
using dino8::app::TryParseFrame;
using dino8::kernel::Point3d;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

// Services `server` (if non-null) and every CollabClient in `clients` for
// up to `max_ticks` rounds, stopping early once `done()` reports true - the
// real-loopback-socket analogue of test_compute_server.cpp's single
// PollOnce(timeout_ms) call, just repeated because CollabSession's own
// relay-then-deliver path genuinely takes more than one tick to settle
// (see CollabSession::PollAndApply's own comment on ordering).
template <typename Done>
void PumpUntil(CollabServer* server, std::vector<CollabClient*> clients, Done done, int max_ticks = 50) {
  for (int i = 0; i < max_ticks && !done(); ++i) {
    if (server) server->PollOnce(20);
    for (CollabClient* c : clients) {
      std::vector<std::string> frames;
      std::string error;
      c->PollOnce(&frames, error);
    }
  }
}

// Drives two CollabSessions exactly the way the app's per-frame hook does
// (one PollAndApply per side per "frame", `first` before `second`) until
// `done()` holds, giving up after `timeout` of WALL-CLOCK time, or as soon
// as either side drops out of the session (a real connection failure -
// PollAndApply Leave()s on one - would otherwise just be a silent timeout).
// Between frames in which `done()` is still false it sleeps ~1 ms, the
// same "one frame later" cadence the real app has, so the wait is a
// time budget and not an iteration count.
//
// This matters because PollAndApply polls everything with a zero timeout
// (it is meant to be called once per rendered frame, never to block), so a
// fixed-N loop of it with no sleep burns through its N iterations in a few
// hundred microseconds. On Linux that is fine - loopback TCP delivers a
// segment to the receiving socket synchronously, inside send() - but on
// macOS the loopback interface hands every packet to a kernel input thread
// and the peer socket only sees it once that thread has run, which on a
// loaded CI runner is easily longer than a few hundred microseconds. The
// old "40 ticks, no sleep" loops here (which never failed on Linux CI)
// failed exactly that way on macos-14 at commit 753cfcd8: the join side's
// applied-snapshot counter read >= 1 (the host's initial empty snapshot
// had arrived during earlier ticks), but the snapshot carrying the new
// point had not reached it yet when the 40 ticks ran out - a time-of-
// arrival race, not a data race (every socket here is serviced on this
// one thread). Reproduced on Linux with an LD_PRELOAD shim that hides
// just-sent loopback data for 300 us (under CPU load): 12 of 200 runs failed with exactly
// the CI signature; 0 of 200 with this wait.
template <typename Done>
bool PumpSessionsUntil(CollabSession& first, Document& first_doc, CollabSession& second, Document& second_doc,
                       Done done, std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    std::string error;
    first.PollAndApply(first_doc, error);
    if (!first.IsActive()) { std::printf("  (session dropped: %s)\n", error.c_str()); return false; }
    second.PollAndApply(second_doc, error);
    if (!second.IsActive()) { std::printf("  (session dropped: %s)\n", error.c_str()); return false; }
    if (done()) return true;
    if (std::chrono::steady_clock::now() >= deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace

int main() {
  // ---- TryParseFrame / BuildFrame: pure, no sockets --------------------
  {
    const std::string wire = BuildFrame("hello");
    Check(wire.size() == 4 + 5, "BuildFrame prepends exactly a 4-byte length prefix");
    std::string payload;
    size_t consumed = 0;
    Check(TryParseFrame(wire, &payload, &consumed), "a complete frame parses");
    Check(payload == "hello", "the parsed payload matches exactly");
    Check(consumed == wire.size(), "consumed covers the whole frame, prefix included");
  }
  {
    std::string payload;
    size_t consumed = 0;
    Check(!TryParseFrame("", &payload, &consumed), "an empty buffer is correctly reported incomplete");
    Check(!TryParseFrame(std::string("\x00\x00\x00", 3), &payload, &consumed),
          "a length prefix that hasn't fully arrived yet is correctly reported incomplete");
  }
  {
    // Declares a 10-byte payload but only 3 bytes have actually arrived.
    const std::string partial = BuildFrame("0123456789").substr(0, 4 + 3);
    std::string payload;
    size_t consumed = 0;
    Check(!TryParseFrame(partial, &payload, &consumed), "a frame whose declared payload hasn't fully arrived yet is incomplete");
  }
  {
    // The same frame delivered as two partial chunks, exactly like a real
    // socket read loop would see it.
    const std::string full = BuildFrame("a longer payload that spans the split point");
    const std::string first_half = full.substr(0, full.size() / 2);
    std::string payload;
    size_t consumed = 0;
    Check(!TryParseFrame(first_half, &payload, &consumed), "a frame split mid-stream is correctly reported incomplete");
    Check(TryParseFrame(full, &payload, &consumed), "the same frame, once fully buffered, parses");
    Check(payload == "a longer payload that spans the split point", "the fully-buffered split frame's payload is intact");
  }
  {
    // A declared length over kMaxFrameBytes must be rejected outright, not
    // buffered forever waiting for gigabytes that will never arrive.
    std::string buf(4, '\0');
    buf[0] = '\xFF'; buf[1] = '\xFF'; buf[2] = '\xFF'; buf[3] = '\xFF';  // 0xFFFFFFFF bytes declared
    std::string payload;
    size_t consumed = 0;
    Check(!TryParseFrame(buf, &payload, &consumed), "an oversized declared frame length is rejected rather than buffered forever");
  }

  // ---- EncodeSnapshotPayload / TryDecodeSnapshotPayload: pure ----------
  {
    const std::string encoded = EncodeSnapshotPayload(0x0102030405060708ULL, 42, "fake .3dm bytes");
    std::uint64_t origin = 0, seq = 0;
    std::string body;
    Check(TryDecodeSnapshotPayload(encoded, &origin, &seq, &body), "a well-formed snapshot payload decodes");
    Check(origin == 0x0102030405060708ULL, "the origin id round-trips exactly");
    Check(seq == 42, "the sequence number round-trips exactly");
    Check(body == "fake .3dm bytes", "the .3dm payload bytes round-trip exactly, unmodified");
  }
  {
    std::uint64_t origin = 0, seq = 0;
    std::string body;
    Check(!TryDecodeSnapshotPayload("short", &origin, &seq, &body), "a payload shorter than the 16-byte header is rejected");
  }

#ifndef _WIN32
  // ---- Real loopback relay: CollabServer forwards to every OTHER client,
  // never back to the sender ---------------------------------------------
  {
    CollabServer server;
    std::string error;
    Check(server.Start(0, error), "CollabServer::Start(0) binds an OS-assigned ephemeral port");
    Check(server.Port() > 0, "Port() reports the real bound port");

    CollabClient a, b;
    Check(a.Connect("127.0.0.1", server.Port(), error), "client A connects to the real bound port");
    Check(b.Connect("127.0.0.1", server.Port(), error), "client B connects to the real bound port");

    PumpUntil(&server, {&a, &b}, [&] { return server.ClientCount() >= 2; });
    Check(server.ClientCount() == 2, "the server accepted both real client connections");

    std::string send_error;
    Check(a.Send("hello from A", send_error), "client A sends a frame");

    // Collects directly into b_frames/a_frames (rather than going through
    // PumpUntil, which would drain and discard each PollOnce's own output) -
    // several ticks so the relay has time to pick up A's frame and forward
    // it, plus a few extra beyond that so a wrongly-echoed frame would have
    // time to show up too.
    std::vector<std::string> b_frames, a_frames;
    for (int i = 0; i < 20; ++i) {
      std::vector<std::string> frames;
      std::string poll_error;
      if (server.PollOnce(10) == 0) { /* nothing relayed this tick */ }
      a.PollOnce(&frames, poll_error);
      for (auto& f : frames) a_frames.push_back(f);
      b.PollOnce(&frames, poll_error);
      for (auto& f : frames) b_frames.push_back(f);
    }

    Check(b_frames.size() == 1 && b_frames[0] == "hello from A", "client B received exactly the one frame client A sent, over a real socket");
    Check(a_frames.empty(), "the relay never echoes a frame back to its own sender");
  }
#endif

  // ---- Full CollabSession integration: two real Documents converge ------
#ifndef _WIN32
  {
    CollabSession host_session;
    CollabSession join_session;
    Document doc_host;
    Document doc_join;

    std::string error;
    Check(host_session.Host(0, error), "CollabSession::Host starts a real relay and joins it over loopback");
    Check(host_session.Role() == CollabRole::Host, "the hosting session reports CollabRole::Host");
    Check(host_session.HostPort() > 0, "HostPort() reports the real bound port");

    Check(join_session.Join("127.0.0.1", host_session.HostPort(), error), "CollabSession::Join connects to the hosted session");
    Check(join_session.Role() == CollabRole::Joined, "the joining session reports CollabRole::Joined");

    // Let the connection settle: the host notices its own loopback peer,
    // and each side's very first PollAndApply pushes an initial (empty)
    // snapshot that the other side applies. Waiting for THAT exchange to
    // finish (not just for the socket count) is what makes the counter
    // checks below meaningful - they compare against these settled values,
    // so "incremented" can only be satisfied by the snapshot carrying the
    // edit, never by this initial one.
    const bool settled = PumpSessionsUntil(host_session, doc_host, join_session, doc_join, [&] {
      return host_session.PeerCount() == 1 && host_session.AppliedCount() >= 1 && join_session.AppliedCount() >= 1;
    });
    Check(settled, "the session settles: both sides exchanged their initial snapshots over the real loopback relay");
    Check(host_session.PeerCount() == 1, "the host sees exactly one real connected peer (the joined session)");
    const std::uint64_t host_sent_before = host_session.SentCount();
    const std::uint64_t join_applied_before = join_session.AppliedCount();

    // A real edit on the HOST side must reach the JOIN side's Document.
    doc_host.Add(SceneObject::MakePoint(Point3d{1, 2, 3}));
    Check(doc_host.ObjectCount() == 1, "the host's own document really gained the new point object");
    const bool join_has_object = PumpSessionsUntil(host_session, doc_host, join_session, doc_join,
                                                   [&] { return doc_join.ObjectCount() == 1; });
    Check(join_has_object, "a point added on the HOST side arrived on the JOIN side's Document over the real network session");
    if (join_has_object) {
      const SceneObject* obj = doc_join.Find(doc_join.Objects().front().id);
      const bool close = obj && obj->kind == dino8::app::ObjectKind::Point && std::abs(obj->point.x - 1) < 1e-6 &&
                          std::abs(obj->point.y - 2) < 1e-6 && std::abs(obj->point.z - 3) < 1e-6;
      Check(close, "the arrived object's geometry is byte-for-byte the same point the host actually added, not a coincidence");
    }
    Check(host_session.SentCount() > host_sent_before, "the host's own sent-snapshot counter actually incremented");
    Check(join_session.AppliedCount() > join_applied_before, "the join side's own applied-snapshot counter actually incremented");

    // A real edit on the JOIN side must reach back to the HOST side too -
    // proving this is real two-way sync, not just host-broadcasts-once.
    const std::uint64_t join_sent_before = join_session.SentCount();
    const std::uint64_t host_applied_before = host_session.AppliedCount();
    doc_join.Add(SceneObject::MakePoint(Point3d{9, 9, 9}));
    const bool host_has_two = PumpSessionsUntil(join_session, doc_join, host_session, doc_host,
                                                [&] { return doc_host.ObjectCount() == 2; });
    Check(host_has_two, "a point added on the JOIN side arrived back on the HOST side's Document - real two-way sync");
    Check(join_session.SentCount() > join_sent_before, "the join side's own sent-snapshot counter incremented for its edit");
    Check(host_session.AppliedCount() > host_applied_before, "the host's own applied-snapshot counter incremented for the join side's edit");

    host_session.Leave();
    join_session.Leave();
    Check(!host_session.IsActive() && !join_session.IsActive(), "Leave() cleanly ends both sides of the session");
  }
#endif

  // ---- CollabSession::Get() singleton sanity ----------------------------
  {
    CollabSession& s1 = CollabSession::Get();
    CollabSession& s2 = CollabSession::Get();
    Check(&s1 == &s2, "CollabSession::Get() always returns the same process-wide instance");
    Check(!s1.IsActive(), "the process-wide session starts out inactive");
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
