#include "net/CollabSession.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#include "io/File3dm.h"

namespace dino8::app {

namespace {

void AppendBe64(std::string& out, std::uint64_t v) {
  for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
}

std::uint64_t ReadBe64(const std::string& s, size_t pos) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<unsigned char>(s[pos + static_cast<size_t>(i)]);
  return v;
}

std::string TempSnapshotPath(std::uint64_t origin, std::uint64_t seq) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path dir = fs::temp_directory_path(ec);
  if (ec) dir = fs::path(".");
  std::ostringstream name;
  name << "dino8_collab_" << origin << "_" << seq << ".3dm";
  return (dir / name.str()).string();
}

bool ReadWholeFile(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

bool WriteWholeFile(const std::string& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return out.good();
}

std::uint64_t RandomOrigin() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<std::uint64_t> dist;
  return dist(gen);
}

}  // namespace

std::string EncodeSnapshotPayload(std::uint64_t origin, std::uint64_t seq, const std::string& threedm_bytes) {
  std::string out;
  out.reserve(16 + threedm_bytes.size());
  AppendBe64(out, origin);
  AppendBe64(out, seq);
  out += threedm_bytes;
  return out;
}

bool TryDecodeSnapshotPayload(const std::string& payload, std::uint64_t* origin, std::uint64_t* seq,
                               std::string* threedm_bytes) {
  if (payload.size() < 16) return false;
  *origin = ReadBe64(payload, 0);
  *seq = ReadBe64(payload, 8);
  *threedm_bytes = payload.substr(16);
  return true;
}

CollabSession& CollabSession::Get() {
  static CollabSession s;
  return s;
}

bool CollabSession::Host(int port, std::string& error) {
  Leave();
  if (!server_.Start(port, error)) return false;
  if (!client_.Connect("127.0.0.1", server_.Port(), error)) {
    server_.Stop();
    return false;
  }
  hosting_ = true;
  role_ = CollabRole::Host;
  local_origin_ = RandomOrigin();
  local_seq_ = 0;
  has_synced_ = false;
  sent_count_ = 0;
  applied_count_ = 0;
  return true;
}

bool CollabSession::Join(const std::string& host, int port, std::string& error) {
  Leave();
  if (!client_.Connect(host, port, error)) return false;
  hosting_ = false;
  role_ = CollabRole::Joined;
  local_origin_ = RandomOrigin();
  local_seq_ = 0;
  has_synced_ = false;
  sent_count_ = 0;
  applied_count_ = 0;
  return true;
}

void CollabSession::Leave() {
  client_.Disconnect();
  if (hosting_) server_.Stop();
  hosting_ = false;
  role_ = CollabRole::None;
  has_synced_ = false;
}

bool CollabSession::PollAndApply(Document& doc, std::string& error) {
  if (role_ == CollabRole::None) return false;

  // Service the relay first (if hosting) so a snapshot this process is
  // about to Send() below reaches the relay's accept/recv bookkeeping on a
  // later call rather than racing its own send, and so any peer's frame
  // already sitting in the relay gets forwarded before this process reads
  // its own client socket just below.
  if (hosting_) server_.PollOnce(0);

  std::vector<std::string> frames;
  std::string poll_error;
  if (!client_.PollOnce(&frames, poll_error)) {
    error = poll_error;
    Leave();
    return false;
  }

  bool applied = false;
  const std::string original_path = doc.Path();
  for (const std::string& frame : frames) {
    std::uint64_t origin = 0, seq = 0;
    std::string threedm_bytes;
    if (!TryDecodeSnapshotPayload(frame, &origin, &seq, &threedm_bytes)) continue;
    // Defensive echo-guard: CollabServer::PollOnce never relays a frame
    // back to its own sender, so this should never actually trigger, but a
    // future relay topology change (or a test driving CollabSession
    // directly against a hand-built frame) shouldn't silently reload a
    // document from its own just-sent snapshot.
    if (origin == local_origin_) continue;
    const std::string path = TempSnapshotPath(origin, seq);
    if (!WriteWholeFile(path, threedm_bytes)) continue;
    std::string load_error;
    const bool loaded = Load3dm(doc, path, load_error);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (!loaded) continue;
    doc.SetPath(original_path);  // Load3dm's Clear() wipes Path(); a received snapshot shouldn't un-name this document
    last_synced_revision_ = doc.Revision();
    has_synced_ = true;
    ++applied_count_;
    applied = true;
  }

  if (!has_synced_ || doc.Revision() != last_synced_revision_) {
    const std::string path = TempSnapshotPath(local_origin_, ++local_seq_);
    std::string save_error;
    if (Save3dm(doc, path, save_error)) {
      std::string bytes;
      if (ReadWholeFile(path, &bytes)) {
        const std::string payload = EncodeSnapshotPayload(local_origin_, local_seq_, bytes);
        std::string send_error;
        if (client_.Send(payload, send_error)) ++sent_count_;
      }
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }
    last_synced_revision_ = doc.Revision();
    has_synced_ = true;
  }

  return applied;
}

void UpdateCollabSession(Document& doc) {
  CollabSession& session = CollabSession::Get();
  if (!session.IsActive()) return;
  std::string error;
  session.PollAndApply(doc, error);  // a transient failure just leaves the session (Leave()); nothing to report per-frame
}

}  // namespace dino8::app
