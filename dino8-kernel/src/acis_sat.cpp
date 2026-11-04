// ACIS text save-file (.sat) reader and writer, written clean-room from the
// layouts in docs/ACIS_SAT_NOTES.md. See acis_sat.h for the scope.
//
// Reader pipeline: text -> tokens -> records (index = position) -> per body,
// walk body/lump/shell/face/loop/coedge/edge/vertex/point and the geometry
// they point at, building genuine ON_Brep topology (shared vertices and edges,
// one trim per coedge) around exact NURBS surfaces. Anything outside the
// supported set throws SatError with a reason; the body is then skipped and
// the reason is reported - never a degraded body.
//
// SAB (binary) shares everything below the tokenizer: ParseSab() produces the
// same Rec/Tok stream as ParseSat(), so the entity builders, the hardening and
// the writer's record generation are one code path. See "SAB wire format" below
// for the token tag table - every value in it is UNVERIFIED.
//
// Writer pipeline: each live face is classified (plane / cylinder / sphere /
// torus via OpenNURBS' own recognisers), each edge curve as line or circular
// arc; seam edges of periodic faces are dropped (ACIS periodic faces carry
// one loop per boundary circle and no seam) and the result is emitted as
// version-700 records.
#include "dino8/kernel/acis_sat.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>

namespace dino8::kernel {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kTol = 1e-5;  // model-unit tolerance for "same point" / "on curve" checks

struct SatError : std::runtime_error {
  using std::runtime_error::runtime_error;
};
[[noreturn]] void Refuse(const std::string& m) { throw SatError(m); }

// ---------------------------------------------------------------------------
// Tokens and records
// ---------------------------------------------------------------------------

// Hostile-input limits (shared by the text and binary readers). A file or
// stream larger than kMaxFileBytes, or one that would expand to more than
// kMaxTokens tokens / kMaxRecords records, is refused with a stated reason
// instead of allocating. (A token costs ~60 bytes in memory but may be a single
// input byte, hence the separate token cap.)
constexpr size_t kMaxFileBytes = size_t(128) << 20;
constexpr size_t kMaxTokens = size_t(2) << 20;
constexpr size_t kMaxRecords = size_t(1) << 20;
constexpr int kMaxBraceDepth = 64;

struct Tok {
  // Bool: SAB-only two-valued token (forward/reversed, single/double, in/out,
  // forward_v/reverse_v, "I"=false / "F"=true for parameter ranges); the word
  // it stands for depends on the field, so Cur::Id(word_if_false, word_if_true)
  // resolves it. Text files carry the words themselves (Ident).
  enum Kind { Ptr, Num, Ident, Str, LBrace, RBrace, Bool } kind = Ident;
  double d = 0.0;
  long long i = 0;
  std::string s;
};

struct Rec {
  int index = 0;
  std::string type;
  std::vector<Tok> f;
};

struct ParsedFile {
  int version = 0;
  std::vector<std::string> notes;  // tolerated oddities (reported as warnings)
  double units = 1.0, resabs = 1e-6, resnor = 1e-10;
  std::vector<Rec> recs;
};

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Reads the next text line from `text` starting at `pos`.
bool NextLine(const std::string& text, size_t& pos, std::string& line) {
  if (pos >= text.size()) return false;
  size_t e = text.find('\n', pos);
  if (e == std::string::npos) e = text.size();
  line = text.substr(pos, e - pos);
  pos = (e < text.size()) ? e + 1 : e;
  return true;
}

// Header line 2: three length-prefixed strings, "N text" or "@N text".
bool ParseHeaderStrings(const std::string& line, int count) {
  size_t p = 0;
  for (int k = 0; k < count; ++k) {
    while (p < line.size() && IsSpace(line[p])) ++p;
    if (p < line.size() && line[p] == '@') ++p;
    if (p >= line.size() || !std::isdigit(static_cast<unsigned char>(line[p]))) return false;
    size_t len = 0;
    while (p < line.size() && std::isdigit(static_cast<unsigned char>(line[p]))) len = len * 10 + (line[p++] - '0');
    if (p >= line.size() || line[p] != ' ') return false;
    ++p;
    if (p + len > line.size()) return false;
    p += len;
  }
  return true;
}

bool ParseNumberWord(const std::string& w, Tok& t) {
  if (w.empty()) return false;
  const char c0 = w[0];
  if (!(std::isdigit(static_cast<unsigned char>(c0)) || c0 == '-' || c0 == '+' || c0 == '.')) return false;
  char* end = nullptr;
  const double d = std::strtod(w.c_str(), &end);
  if (end != w.c_str() + w.size()) return false;
  t.kind = Tok::Num;
  t.d = d;
  // inf/nan or a value outside long long would make the cast undefined; the integer view is then a harmless sentinel
  t.i = (std::isfinite(d) && std::fabs(d) < 9.0e18) ? static_cast<long long>(d) : std::numeric_limits<long long>::min() + 1;
  return true;
}

// Reads the whole stream, refusing (with a reason) beyond kMaxFileBytes.
std::string ReadAllBounded(std::istream& in) {
  std::string data;
  char buf[1 << 16];
  while (in) {
    in.read(buf, sizeof(buf));
    const std::streamsize got = in.gcount();
    if (got <= 0) break;
    if (data.size() + static_cast<size_t>(got) > kMaxFileBytes) {
      Refuse("input larger than the " + std::to_string(kMaxFileBytes >> 20) + " MiB limit (refused without reading further)");
    }
    data.append(buf, static_cast<size_t>(got));
  }
  return data;
}

const char kSabMagic[] = "ACIS BinaryFile";  // 15 bytes, UNVERIFIED (see "SAB wire format")
constexpr size_t kSabMagicLen = 15;

ParsedFile ParseSat(const std::string& text) {
  if (text.empty()) Refuse("empty file");
  if (text.size() > kMaxFileBytes) Refuse("input larger than the " + std::to_string(kMaxFileBytes >> 20) + " MiB limit");
  if (text.size() >= kSabMagicLen && std::memcmp(text.data(), kSabMagic, kSabMagicLen) == 0) {
    Refuse("this is a binary SAB file, not SAT text (use ReadAcisSab or ReadAcis)");
  }
  ParsedFile pf;
  size_t pos = 0;
  std::string line;
  if (!NextLine(text, pos, line)) Refuse("missing header");
  {
    long long v = 0, nrec = 0, nent = 0, flags = 0;
    if (std::sscanf(line.c_str(), "%lld %lld %lld %lld", &v, &nrec, &nent, &flags) != 4) {
      Refuse("malformed header line 1 (expected: version records entities flags)");
    }
    // The count-guarded layouts below were written for the 4.x-10.x era
    // (versions 400..1100); anything else is refused rather than guessed at.
    if (v < 400 || v > 1100) {
      Refuse("unsupported ACIS save-file version " + std::to_string(v) + " (supported: 400..1100, i.e. ACIS 4.0 to 11.0)");
    }
    pf.version = static_cast<int>(v);
  }
  if (!NextLine(text, pos, line) || !ParseHeaderStrings(line, 3)) {
    Refuse("malformed header line 2 (expected product id, ACIS version and date strings)");
  }
  if (!NextLine(text, pos, line)) Refuse("missing header line 3 (units and tolerances)");
  if (std::sscanf(line.c_str(), "%lf %lf %lf", &pf.units, &pf.resabs, &pf.resnor) != 3 || !(pf.units > 0.0) ||
      !std::isfinite(pf.units) || !std::isfinite(pf.resabs) || !std::isfinite(pf.resnor)) {
    Refuse("malformed header line 3 (expected finite: units resabs resnor, units > 0)");
  }

  std::vector<Tok> cur;
  bool end_marker = false;
  size_t token_count = 0;
  const size_t n = text.size();
  auto finish = [&]() {
    if (cur.empty()) return;
    Rec r;
    size_t first = 0;
    // Optional leading "-N" record index (ACIS 7+), then the type name.
    if (cur[0].kind == Tok::Num && cur.size() > 1 && cur[1].kind == Tok::Ident) first = 1;
    if (cur[first].kind != Tok::Ident) Refuse("record " + std::to_string(pf.recs.size()) + " does not start with an entity name");
    r.type = cur[first].s;
    if (pf.recs.size() >= kMaxRecords) Refuse("more than " + std::to_string(kMaxRecords) + " records (limit)");
    r.index = static_cast<int>(pf.recs.size());
    if (first == 1) {
      const long long ix = std::llabs(cur[0].i);
      if (ix != r.index) {
        Refuse("record index " + std::to_string(ix) + " found where record " + std::to_string(r.index) +
               " was expected (sparse numbering is not supported)");
      }
    }
    r.f.assign(cur.begin() + static_cast<long>(first) + 1, cur.end());
    pf.recs.push_back(std::move(r));
    cur.clear();
  };
  while (pos < n) {
    const char c = text[pos];
    if (IsSpace(c)) {
      ++pos;
      continue;
    }
    if (c == '#') {
      finish();
      ++pos;
      continue;
    }
    Tok t;
    if (c == '{' || c == '}') {
      t.kind = (c == '{') ? Tok::LBrace : Tok::RBrace;
      ++pos;
    } else if (c == '@') {
      ++pos;
      size_t len = 0;
      bool any = false;
      while (pos < n && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        len = len * 10 + (text[pos++] - '0');
        any = true;
      }
      if (!any || pos >= n || !IsSpace(text[pos])) Refuse("malformed @-string");
      ++pos;
      if (pos + len > n) Refuse("truncated file: string runs past the end");
      t.kind = Tok::Str;
      t.s = text.substr(pos, len);
      pos += len;
    } else {
      size_t e = pos;
      while (e < n && !IsSpace(text[e]) && text[e] != '#' && text[e] != '{' && text[e] != '}') ++e;
      const std::string w = text.substr(pos, e - pos);
      pos = e;
      if (cur.empty() && (w == "End-of-ACIS-data" || w == "End-of-ASM-data")) {
        end_marker = true;
        break;
      }
      if (w[0] == '$') {
        t.kind = Tok::Ptr;
        char* endp = nullptr;
        t.i = std::strtoll(w.c_str() + 1, &endp, 10);
        if (endp == w.c_str() + 1 || *endp != '\0') Refuse("malformed pointer '" + w + "'");
      } else if (!ParseNumberWord(w, t)) {
        t.kind = Tok::Ident;
        t.s = w;
      }
    }
    if (++token_count > kMaxTokens) Refuse("more than " + std::to_string(kMaxTokens) + " tokens (limit)");
    cur.push_back(std::move(t));
  }
  if (!cur.empty()) Refuse("truncated file: last record has no terminating '#'");
  if (!end_marker) Refuse("truncated file: missing End-of-ACIS-data marker");
  return pf;
}

// ---------------------------------------------------------------------------
// SAB wire format
//
// *** EVERYTHING IN THIS SECTION IS UNVERIFIED. *** No public prose describing
// the SAB byte layout could be fetched (the one public document used, the
// ezdxf "ACIS Tools" page, only names .sab). The tag values below are the
// author's unsourced recollection of the published format, plus first-
// principles choices where recollection is silent (marked "chosen"). They have
// never been compared with a file produced or consumed by any other product.
// The writer is round-trip tested against THIS reader only.
//
//   file    := "ACIS BinaryFile"(15 bytes)  i32 version  i32 record-count
//              i32 entity-count  i32 flags
//              str str str                      (product id, ACIS version, date)
//              f64 f64 f64                      (units, resabs, resnor)
//              record*  end-record
//   record  := NAME token*  TERM
//   end     := NAME("End-of-ACIS-data") TERM    (chosen)
//   All multi-byte values are little-endian (chosen).
//   The four i32 after the signature are plain (untagged) in the header
//   (chosen); the header strings and doubles carry their tags.
//
//   tag  meaning                         payload
//   0x01 unsigned byte                   u8
//   0x02 signed char                     i8
//   0x03 signed short                    i16
//   0x04 signed int                      i32
//   0x05 float                           f32
//   0x06 double                          f64
//   0x07 string, 8-bit length            u8 len, bytes
//   0x08 string, 16-bit length           u16 len, bytes
//   0x09 string, 32-bit length           u32 len, bytes
//   0x0A bool true                       -
//   0x0B bool false                      -
//   0x0C pointer (record index, -1=null) i32
//   0x0D name (entity type / identifier) u8 len, bytes
//   0x0F '{'  0x10 '}'  0x11 record terminator (no payload)
//   0x13 position                        3 x f64
//   0x14 direction vector                3 x f64
//   Not accepted (refused with a reason): 0x0E, 0x12, 0x15 and anything else.
//
//   Two-valued words are bools here (chosen): forward=false reversed=true,
//   single=false double=true, in=false out=true, forward_v=false
//   reverse_v=true; a parameter range is false ("I", infinite) or
//   true followed by a double ("F value"). Entity type names are one 0x0D token
//   with the dashed name ("plane-surface"), not split (chosen). There is no
//   record index in SAB; a record's index is its position.
// ---------------------------------------------------------------------------
namespace sabtag {
constexpr uint8_t kByte = 0x01, kChar = 0x02, kShort = 0x03, kInt = 0x04, kFloat = 0x05, kDouble = 0x06, kStr8 = 0x07,
                  kStr16 = 0x08, kStr32 = 0x09, kTrue = 0x0A, kFalse = 0x0B, kPtr = 0x0C, kName = 0x0D, kLBrace = 0x0F,
                  kRBrace = 0x10, kTerm = 0x11, kPos = 0x13, kVec = 0x14;
}

constexpr const char kSabEndName[] = "End-of-ACIS-data";

class SabCursor {
 public:
  explicit SabCursor(const std::string& d) : d_(reinterpret_cast<const unsigned char*>(d.data())), n_(d.size()) {}
  size_t pos() const { return p_; }
  size_t remaining() const { return n_ - p_; }
  bool AtEnd() const { return p_ >= n_; }
  uint8_t Peek() const { return d_[p_]; }
  [[noreturn]] void Fail(const std::string& what) const {
    Refuse("SAB file, byte offset " + std::to_string(p_) + ": " + what);
  }
  void Need(size_t k, const char* what) const {
    if (n_ - p_ < k) Fail(std::string("truncated file: ") + what + " needs " + std::to_string(k) + " byte(s), " + std::to_string(n_ - p_) + " left");
  }
  uint8_t U8(const char* what) {
    Need(1, what);
    return d_[p_++];
  }
  uint32_t U32(const char* what) {
    Need(4, what);
    const uint32_t v = uint32_t(d_[p_]) | (uint32_t(d_[p_ + 1]) << 8) | (uint32_t(d_[p_ + 2]) << 16) | (uint32_t(d_[p_ + 3]) << 24);
    p_ += 4;
    return v;
  }
  int32_t I32(const char* what) { return static_cast<int32_t>(U32(what)); }
  uint16_t U16(const char* what) {
    Need(2, what);
    const uint16_t v = static_cast<uint16_t>(d_[p_] | (d_[p_ + 1] << 8));
    p_ += 2;
    return v;
  }
  double F64(const char* what) {
    Need(8, what);
    uint64_t u = 0;
    for (int i = 7; i >= 0; --i) u = (u << 8) | d_[p_ + static_cast<size_t>(i)];
    p_ += 8;
    double v;
    std::memcpy(&v, &u, 8);
    if (!std::isfinite(v)) Fail(std::string("non-finite (inf/nan) ") + what);
    return v;
  }
  double F32(const char* what) {
    const uint32_t u = U32(what);
    float f;
    std::memcpy(&f, &u, 4);
    if (!std::isfinite(f)) Fail(std::string("non-finite (inf/nan) ") + what);
    return static_cast<double>(f);
  }
  // `len` bytes of payload; the length is checked against the data left BEFORE anything is allocated.
  std::string Bytes(size_t len, const char* what) {
    if (len > n_ - p_) {
      Fail(std::string(what) + " length " + std::to_string(len) + " runs past the end of the file (" + std::to_string(n_ - p_) +
           " bytes left)");
    }
    std::string s(reinterpret_cast<const char*>(d_ + p_), len);
    p_ += len;
    return s;
  }

 private:
  const unsigned char* d_;
  size_t n_;
  size_t p_ = 0;
};

Tok MakeNumTok(double d) {
  Tok t;
  t.kind = Tok::Num;
  t.d = d;
  t.i = (std::isfinite(d) && std::fabs(d) < 9.0e18) ? static_cast<long long>(d) : std::numeric_limits<long long>::min() + 1;
  return t;
}

// A header/body string token: tag 0x07/0x08/0x09 already consumed.
std::string SabString(SabCursor& c, uint8_t tag) {
  size_t len = 0;
  if (tag == sabtag::kStr8) len = c.U8("string length");
  else if (tag == sabtag::kStr16) len = c.U16("string length");
  else len = c.U32("string length");
  return c.Bytes(len, "string");
}

bool IsSabStringTag(uint8_t t) { return t == sabtag::kStr8 || t == sabtag::kStr16 || t == sabtag::kStr32; }

ParsedFile ParseSab(const std::string& data) {
  if (data.empty()) Refuse("empty file");
  if (data.size() > kMaxFileBytes) Refuse("input larger than the " + std::to_string(kMaxFileBytes >> 20) + " MiB limit");
  if (!LooksLikeAcisSab(data)) Refuse("not an ACIS SAB file (missing the 'ACIS BinaryFile' signature)");
  SabCursor c(data);
  c.Bytes(kSabMagicLen, "signature");
  ParsedFile pf;
  const int32_t version = c.I32("version");
  const int32_t declared_records = c.I32("record count");
  const int32_t declared_entities = c.I32("entity count");
  c.I32("flags");
  if (version < 400 || version > 1100) {
    Refuse("unsupported ACIS save-file version " + std::to_string(version) + " (supported: 400..1100, i.e. ACIS 4.0 to 11.0)");
  }
  pf.version = version;
  if (declared_records < 0 || declared_entities < 0) c.Fail("negative record or entity count in the header");
  // Every record needs at least 3 bytes (name tag + length + terminator at the very least): a larger declared count is a lie.
  if (static_cast<uint64_t>(declared_records) > c.remaining() / 3) {
    c.Fail("header declares " + std::to_string(declared_records) + " records, which cannot fit in the " + std::to_string(c.remaining()) +
           " bytes that follow");
  }
  for (int k = 0; k < 3; ++k) {
    const uint8_t tag = c.U8("header string tag");
    if (!IsSabStringTag(tag)) c.Fail("malformed header: expected a string tag (product id, ACIS version, date)");
    SabString(c, tag);
  }
  double hd[3];
  for (int k = 0; k < 3; ++k) {
    if (c.U8("header number tag") != sabtag::kDouble) c.Fail("malformed header: expected a double tag (units, resabs, resnor)");
    hd[k] = c.F64("header number");
  }
  pf.units = hd[0];
  pf.resabs = hd[1];
  pf.resnor = hd[2];
  if (!(pf.units > 0.0)) c.Fail("malformed header: units must be > 0");

  size_t token_count = 0;
  bool end_marker = false;
  while (!c.AtEnd()) {
    if (c.U8("record") != sabtag::kName) c.Fail("record " + std::to_string(pf.recs.size()) + " does not start with an entity name token");
    const size_t nlen = c.U8("entity name length");
    const std::string name = c.Bytes(nlen, "entity name");
    if (name.empty()) c.Fail("empty entity name");
    for (char ch : name) {
      if (static_cast<unsigned char>(ch) < 0x21 || static_cast<unsigned char>(ch) > 0x7E) c.Fail("entity name contains a non-printable byte");
    }
    if (name == kSabEndName || name == "End-of-ASM-data") {
      if (c.U8("end-record terminator") != sabtag::kTerm) c.Fail("the End-of-ACIS-data record is not terminated");
      if (!c.AtEnd()) pf.notes.push_back("SAB: " + std::to_string(c.remaining()) + " byte(s) after the end marker were ignored");
      end_marker = true;
      break;
    }
    if (pf.recs.size() >= kMaxRecords) c.Fail("more than " + std::to_string(kMaxRecords) + " records (limit)");
    Rec r;
    r.type = name;
    r.index = static_cast<int>(pf.recs.size());
    int depth = 0;
    bool terminated = false;
    while (!terminated) {
      if (c.AtEnd()) c.Fail("truncated file: record " + std::to_string(r.index) + " (" + name + ") has no terminator");
      const uint8_t tag = c.U8("token tag");
      if (++token_count > kMaxTokens) c.Fail("more than " + std::to_string(kMaxTokens) + " tokens (limit)");
      switch (tag) {
        case sabtag::kTerm:
          terminated = true;
          break;
        case sabtag::kByte:
          r.f.push_back(MakeNumTok(c.U8("byte")));
          break;
        case sabtag::kChar:
          r.f.push_back(MakeNumTok(static_cast<int8_t>(c.U8("char"))));
          break;
        case sabtag::kShort:
          r.f.push_back(MakeNumTok(static_cast<int16_t>(c.U16("short"))));
          break;
        case sabtag::kInt:
          r.f.push_back(MakeNumTok(c.I32("int")));
          break;
        case sabtag::kFloat:
          r.f.push_back(MakeNumTok(c.F32("float")));
          break;
        case sabtag::kDouble:
          r.f.push_back(MakeNumTok(c.F64("double")));
          break;
        case sabtag::kStr8:
        case sabtag::kStr16:
        case sabtag::kStr32: {
          Tok t;
          t.kind = Tok::Str;
          t.s = SabString(c, tag);
          r.f.push_back(std::move(t));
          break;
        }
        case sabtag::kTrue:
        case sabtag::kFalse: {
          Tok t;
          t.kind = Tok::Bool;
          t.i = (tag == sabtag::kTrue) ? 1 : 0;
          r.f.push_back(std::move(t));
          break;
        }
        case sabtag::kPtr: {
          Tok t;
          t.kind = Tok::Ptr;
          t.i = c.I32("pointer");
          r.f.push_back(std::move(t));
          break;
        }
        case sabtag::kName: {
          Tok t;
          t.kind = Tok::Ident;
          t.s = c.Bytes(c.U8("identifier length"), "identifier");
          r.f.push_back(std::move(t));
          break;
        }
        case sabtag::kLBrace:
        case sabtag::kRBrace: {
          if (tag == sabtag::kLBrace) {
            if (++depth > kMaxBraceDepth) c.Fail("sub-type nesting deeper than " + std::to_string(kMaxBraceDepth));
          } else if (--depth < 0) {
            c.Fail("unbalanced '}' in record " + std::to_string(r.index));
          }
          Tok t;
          t.kind = (tag == sabtag::kLBrace) ? Tok::LBrace : Tok::RBrace;
          r.f.push_back(std::move(t));
          break;
        }
        case sabtag::kPos:
        case sabtag::kVec:
          token_count += 2;
          if (token_count > kMaxTokens) c.Fail("more than " + std::to_string(kMaxTokens) + " tokens (limit)");
          for (int k = 0; k < 3; ++k) r.f.push_back(MakeNumTok(c.F64("coordinate")));
          break;
        default: {
          char hex[8];
          std::snprintf(hex, sizeof(hex), "0x%02X", tag);
          c.Fail(std::string("token tag ") + hex + " is not in the (UNVERIFIED) tag table this reader knows");
        }
      }
    }
    if (depth != 0) c.Fail("unbalanced '{' in record " + std::to_string(r.index));
    pf.recs.push_back(std::move(r));
  }
  if (!end_marker) Refuse("truncated SAB file: missing the End-of-ACIS-data record");
  if (declared_records > 0 && static_cast<size_t>(declared_records) != pf.recs.size()) {
    pf.notes.push_back("SAB: header declares " + std::to_string(declared_records) + " records but " + std::to_string(pf.recs.size()) + " were found");
  }
  return pf;
}

// Field cursor over one record.
struct Cur {
  const Rec& r;
  double scale;
  int h;
  size_t p = 0;
  [[noreturn]] void Fail(const std::string& what) const {
    Refuse(r.type + " record " + std::to_string(r.index) + ": " + what);
  }
  bool More() const { return p < r.f.size(); }
  const Tok& Next() {
    if (!More()) Fail("too few fields (truncated or unsupported layout)");
    return r.f[p++];
  }
  int Ptr() {
    const Tok& t = Next();
    if (t.kind != Tok::Ptr) Fail("expected a $pointer");
    // A pointer outside int range must not wrap onto a valid record index.
    if (t.i < -1 || t.i > 2000000000LL) Fail("pointer $" + std::to_string(t.i) + " is outside the file's records");
    return static_cast<int>(t.i);
  }
  double Real() {
    const Tok& t = Next();
    if (t.kind != Tok::Num) Fail("expected a number");
    if (!std::isfinite(t.d)) Fail("non-finite number (inf/nan)");
    return t.d;
  }
  // A keyword. In a SAB file the same field is a bool: `if_false` / `if_true` say which word each value stands for.
  std::string Id(const char* if_false = nullptr, const char* if_true = nullptr) {
    const Tok& t = Next();
    if (t.kind == Tok::Bool && if_false && if_true) return t.i ? if_true : if_false;
    if (t.kind != Tok::Ident) Fail("expected a keyword");
    return t.s;
  }
  bool PeekNum() const { return More() && r.f[p].kind == Tok::Num; }
  bool PeekStr() const { return More() && r.f[p].kind == Tok::Str; }
  void End() const {
    if (More()) Fail("unexpected extra fields (unsupported layout)");
  }
  void Header() {
    Ptr();  // attribute
    if (h >= 2) Real();  // entity id
    if (h == 3) Ptr();   // third header item (UNVERIFIED: history pointer)
  }
  ON_3dPoint Pt() {
    const double x = Real(), y = Real(), z = Real();
    return ON_3dPoint(x * scale, y * scale, z * scale);
  }
  ON_3dVector Vec() {
    const double x = Real(), y = Real(), z = Real();
    return ON_3dVector(x, y, z);
  }
  ON_3dVector VecScaled() {
    ON_3dVector v = Vec();
    return v * scale;
  }
  // `n` range items: "I" (infinite) or "F value". Absent items are tolerated.
  void Ranges(int n) {
    for (int k = 0; k < n && More(); ++k) {
      const std::string id = Id("I", "F");
      if (id == "F") {
        Real();
      } else if (id != "I") {
        Fail("bad parameter-range marker '" + id + "'");
      }
    }
  }
};

bool Unit(ON_3dVector& v) { return v.Unitize(); }

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

// Rational quadratic unit-circle NURBS pieces (4 quarter arcs, knots 0..4).
// Parameter u in [q, q+1] is quadrant q; within a quadrant the parameter is
// the rational-Bezier parameter, so angle -> u needs the inverse below.
double QuarterTan(double s) {
  const double w = std::sqrt(0.5);
  const double y = 2.0 * s * (1.0 - s) * w + s * s;
  const double x = (1.0 - s) * (1.0 - s) + 2.0 * s * (1.0 - s) * w;
  return std::atan2(y, x);
}
// Maps an angle phi in [0, 2pi] (measured from the surface's u=0 direction)
// to the surface u parameter in [0, 4].
double UOfPhi(double phi) {
  phi = std::min(std::max(phi, 0.0), kTwoPi);
  int q = static_cast<int>(std::floor(phi / (kPi / 2.0)));
  if (q > 3) q = 3;
  const double local = phi - q * (kPi / 2.0);
  double lo = 0.0, hi = 1.0;
  for (int it = 0; it < 60; ++it) {
    const double mid = 0.5 * (lo + hi);
    if (QuarterTan(mid) < local) lo = mid; else hi = mid;
  }
  return q + 0.5 * (lo + hi);
}

// Exact rational NURBS cone/cylinder: radius r(z) = radius_at_center + k*z,
// axis through `c` along unit `axis`, u=0 at angle a0 about the axis (measured
// from `e1` towards axis x e1), v = z in [zmin, zmax].
ON_NurbsSurface* MakeConeNurbs(const ON_3dPoint& c, const ON_3dVector& axis, const ON_3dVector& e1,
                               double radius_at_center, double k, double a0, double zmin, double zmax) {
  const ON_3dVector e2 = ON_CrossProduct(axis, e1);
  const ON_3dVector ea = std::cos(a0) * e1 + std::sin(a0) * e2;
  const ON_3dVector eb = ON_CrossProduct(axis, ea);
  const double w = std::sqrt(0.5);
  const double cx[9] = {1, 1, 0, -1, -1, -1, 0, 1, 1};
  const double cy[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
  const double cw[9] = {1, w, 1, w, 1, w, 1, w, 1};
  auto* s = new ON_NurbsSurface(3, true, 3, 2, 9, 2);
  const double uk[10] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4};
  for (int i = 0; i < 10; ++i) s->SetKnot(0, i, uk[i]);
  s->SetKnot(1, 0, zmin);
  s->SetKnot(1, 1, zmax);
  for (int i = 0; i < 9; ++i) {
    for (int j = 0; j < 2; ++j) {
      const double z = (j == 0) ? zmin : zmax;
      const double r = radius_at_center + k * z;
      const ON_3dPoint p = c + axis * z + (ea * cx[i] + eb * cy[i]) * r;
      s->SetCV(i, j, ON_4dPoint(p.x * cw[i], p.y * cw[i], p.z * cw[i], cw[i]));
    }
  }
  return s;
}

// Bilinear plane patch P(u,v) = o + u*U + v*V over [u0,u1]x[v0,v1].
ON_NurbsSurface* MakePlaneNurbs(const ON_3dPoint& o, const ON_3dVector& U, const ON_3dVector& V, double u0, double u1,
                                double v0, double v1) {
  auto* s = new ON_NurbsSurface(3, false, 2, 2, 2, 2);
  s->SetKnot(0, 0, u0);
  s->SetKnot(0, 1, u1);
  s->SetKnot(1, 0, v0);
  s->SetKnot(1, 1, v1);
  s->SetCV(0, 0, o + U * u0 + V * v0);
  s->SetCV(1, 0, o + U * u1 + V * v0);
  s->SetCV(0, 1, o + U * u0 + V * v1);
  s->SetCV(1, 1, o + U * u1 + V * v1);
  return s;
}

// Exact NURBS for the elliptical arc  C + cos(t)*major + sin(t)*minor, t in [t0,t1].
ON_NurbsCurve* MakeEllipseArc(const ON_3dPoint& c, const ON_3dVector& major, const ON_3dVector& minor,
                              const ON_3dVector& normal, double t0, double t1) {
  ON_Arc arc(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0)), 1.0),
             ON_Interval(t0, t1));
  auto* nc = new ON_NurbsCurve();
  if (!arc.GetNurbForm(*nc)) {
    delete nc;
    return nullptr;
  }
  ON_Xform xf(ON_Xform::IdentityTransformation);
  for (int i = 0; i < 3; ++i) {
    xf.m_xform[i][0] = major[i];
    xf.m_xform[i][1] = minor[i];
    xf.m_xform[i][2] = normal[i];
    xf.m_xform[i][3] = c[i];
  }
  nc->Transform(xf);
  return nc;
}

double SignedAreaUV(const std::vector<ON_2dPoint>& pts) {
  double a = 0.0;
  const size_t n = pts.size();
  for (size_t i = 0; i < n; ++i) {
    const ON_2dPoint& p = pts[i];
    const ON_2dPoint& q = pts[(i + 1) % n];
    a += p.x * q.y - q.x * p.y;
  }
  return 0.5 * a;
}

std::string Num(double v) {
  if (!std::isfinite(v)) v = 0.0;
  if (v == 0.0) return "0";
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.17g", v);
  return buf;
}

// ---------------------------------------------------------------------------
// Reader: records -> Brep
// ---------------------------------------------------------------------------

struct Ctx {
  const ParsedFile& pf;
  double scale = 1.0;
  int h = 3;
  bool allow = false;
  std::vector<std::string>* warnings = nullptr;
  // Work budget: every record fetched counts. A well-formed file touches each record a handful of times; a file
  // whose pointers make the walk revisit records over and over (shared loops, rings that never close) runs out.
  long long visits = 0;
  long long max_visits = 0;
};

class BodyBuilder {
 public:
  explicit BodyBuilder(Ctx& c) : c_(c), brep_(out_.raw()) {}

  Brep Build(int body_idx) {
    const Rec& body = Get(body_idx, "body");
    Cur cu = MakeCur(body);
    cu.Header();
    // Documented attribute order: pattern, lump, wire, transform. A record
    // with only three pointers (no pattern) is tolerated.
    std::vector<int> bp;
    while (cu.More()) bp.push_back(cu.Ptr());
    if (bp.size() != 3 && bp.size() != 4) cu.Fail("unsupported layout (expected pattern, lump, wire, transform)");
    const size_t o = bp.size() - 3;
    const int lump = bp[o];
    const int wire = bp[o + 1];
    const int xform = bp[o + 2];
    if (wire >= 0) Refuse("body " + std::to_string(body_idx) + " has a wire (wire bodies are not supported)");
    if (xform >= 0) CheckIdentityTransform(xform);
    if (lump < 0) Refuse("body " + std::to_string(body_idx) + " has no lump");
    std::set<int> seen_lumps;
    for (int l = lump; l >= 0;) {
      if (!seen_lumps.insert(l).second) Refuse("lump chain does not terminate (loops back to lump record " + std::to_string(l) + ")");
      const Rec& lr = Get(l, "lump");
      Cur lc = MakeCur(lr);
      lc.Header();
      const int next_lump = lc.Ptr();
      const int shell = lc.Ptr();
      lc.Ptr();  // owning body
      lc.End();
      std::set<int> seen_shells;
      for (int s = shell; s >= 0;) {
        if (!seen_shells.insert(s).second) Refuse("shell chain does not terminate (loops back to shell record " + std::to_string(s) + ")");
        const Rec& sr = Get(s, "shell");
        Cur sc = MakeCur(sr);
        sc.Header();
        const int next_shell = sc.Ptr();
        const int subshell = sc.Ptr();
        const int face = sc.Ptr();
        const int swire = sc.Ptr();
        sc.Ptr();  // lump
        sc.End();
        if (subshell >= 0) Refuse("shell " + std::to_string(s) + " has a subshell (not supported)");
        if (swire >= 0) Refuse("shell " + std::to_string(s) + " has a wire (not supported)");
        std::set<int> seen_faces;  // a face pointing back into its own chain would build the same face again and again
        for (int f = face; f >= 0;) {
          if (!seen_faces.insert(f).second) Refuse("face chain of shell " + std::to_string(s) + " loops back to face record " + std::to_string(f));
          f = BuildFace(f);
        }
        s = next_shell;
      }
      l = next_lump;
    }
    if (brep_.m_F.Count() == 0) Refuse("body " + std::to_string(body_idx) + " has no faces");
    if (whole_quadric_ && brep_.m_F.Count() != 1) Refuse("a whole sphere/torus face combined with other faces is not supported");
    brep_.SetTrimIsoFlags();
    brep_.SetTolerancesBoxesAndFlags(/*bLazy=*/true);
    if (!any_loopless_) {
      ON_TextLog log;
      std::ostringstream os;
      ON_wString ws;
      ON_TextLog tl(ws);
      if (!brep_.IsValid(&tl)) {
        std::string msg = ON_String(ws).Array() ? std::string(ON_String(ws).Array()) : std::string();
        if (msg.size() > 200) msg.resize(200);
        Refuse("body " + std::to_string(body_idx) + " built an invalid ON_Brep (" + msg + ")");
      }
    }
    return std::move(out_);
  }

 private:
  struct EdgeInfo {
    int on_edge = -1;
    int v0 = -1, v1 = -1;
    std::unique_ptr<ON_Curve> c3;  // runs start vertex -> end vertex
    bool is_line = false;
    bool is_circle = false;  // ellipse with ratio 1
    ON_3dPoint center;
    ON_3dVector normal;
    double radius = 0.0;
  };
  struct CoedgeRef {
    int edge;
    bool rev;
  };
  struct TrimSpec {
    int edge = -1;  // ACIS edge index, or -1 with seam_edge set
    bool rev = false;
    bool seam = false;
    int seam_edge = -1;
    std::unique_ptr<ON_Curve> c2;
  };
  struct LoopSpec {
    std::vector<TrimSpec> trims;
    bool outer = true;
  };

  Cur MakeCur(const Rec& r) const { return Cur{r, c_.scale, c_.h}; }

  [[noreturn]] static void Unverified(const std::string& type) {
    Refuse("entity '" + type +
           "' has a field layout that is not stated in the public documentation used (UNVERIFIED, see "
           "docs/ACIS_SAT_NOTES.md); it is refused unless AcisSatOptions::allow_unverified_layouts is set");
  }

  const Rec& GetAny(int idx) const {
    if (++c_.visits > c_.max_visits) {
      Refuse("work limit exceeded: the file's pointers make the reader revisit records far more often than a valid body would (cyclic or shared chains)");
    }
    if (idx < 0 || idx >= static_cast<int>(c_.pf.recs.size())) {
      Refuse("pointer $" + std::to_string(idx) + " is outside the file's " + std::to_string(c_.pf.recs.size()) + " records");
    }
    return c_.pf.recs[static_cast<size_t>(idx)];
  }
  const Rec& Get(int idx, const char* type) const {
    const Rec& r = GetAny(idx);
    if (r.type != type) {
      Refuse("record " + std::to_string(idx) + " is '" + r.type + "' where '" + type + "' was expected");
    }
    return r;
  }

  void CheckIdentityTransform(int idx) {
    const Rec& r = Get(idx, "transform");
    Cur c = MakeCur(r);
    c.Header();
    double m[13];
    for (double& v : m) v = c.Real();
    const double id[13] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 13; ++i) {
      if (std::fabs(m[i] - id[i]) > 1e-12) Refuse("body has a non-identity transform (not supported)");
    }
  }

  ON_3dPoint PointOfVertex(int v) {
    const Rec& vr = Get(v, "vertex");
    Cur c = MakeCur(vr);
    c.Header();
    c.Ptr();  // edge
    const int pt = c.Ptr();
    c.End();
    if (pt < 0) Refuse("vertex " + std::to_string(v) + " has no point");
    const Rec& pr = Get(pt, "point");
    Cur pc = MakeCur(pr);
    pc.Header();
    const ON_3dPoint p = pc.Pt();
    pc.End();
    return p;
  }

  int OnVertex(int acis_v, const ON_3dPoint& p) {
    if (acis_v >= 0) {
      auto it = vertex_of_.find(acis_v);
      if (it != vertex_of_.end()) return it->second;
    }
    ON_BrepVertex& bv = brep_.NewVertex(p, 0.0);
    if (acis_v >= 0) vertex_of_[acis_v] = bv.m_vertex_index;
    return bv.m_vertex_index;
  }

  EdgeInfo& GetEdge(int idx) {
    auto it = edges_.find(idx);
    if (it != edges_.end()) return it->second;
    const Rec& r = Get(idx, "edge");
    Cur c = MakeCur(r);
    c.Header();
    const int sv = c.Ptr();
    const double t0 = c.Real();
    const int ev = c.Ptr();
    const double t1 = c.Real();
    c.Ptr();  // coedge
    const int curve = c.Ptr();
    const std::string sense = c.Id("forward", "reversed");
    if (c.PeekStr()) c.Next();  // convexity string
    c.End();
    if (sense != "forward" && sense != "reversed") c.Fail("bad sense '" + sense + "'");
    if (curve < 0) Refuse("edge " + std::to_string(idx) + " has no curve");
    const bool revsense = (sense == "reversed");
    const Rec& cr = GetAny(curve);
    const bool have_s = sv >= 0, have_e = ev >= 0;
    ON_3dPoint sp, ep;
    if (have_s) sp = PointOfVertex(sv);
    if (have_e) ep = PointOfVertex(ev);
    EdgeInfo info;
    if (cr.type == "straight-curve") {
      Cur cc = MakeCur(cr);
      cc.Header();
      const ON_3dPoint root = cc.Pt();
      ON_3dVector dir = cc.Vec();
      cc.Ranges(2);
      cc.End();
      if (!Unit(dir)) cc.Fail("zero direction");
      const ON_3dPoint a = have_s ? sp : root + dir * (t0 * c_.scale);
      const ON_3dPoint b = have_e ? ep : root + dir * (t1 * c_.scale);
      auto off_line = [&](const ON_3dPoint& p) {
        const ON_3dVector d = p - root;
        return (d - (d * dir) * dir).Length();
      };
      if (off_line(a) > kTol || off_line(b) > kTol) {
        Refuse("edge " + std::to_string(idx) + ": a vertex does not lie on the edge's straight-curve");
      }
      if (a.DistanceTo(b) < kTol) Refuse("edge " + std::to_string(idx) + " is a zero-length line");
      info.c3.reset(new ON_LineCurve(a, b));
      info.is_line = true;
      info.v0 = OnVertex(sv, a);
      info.v1 = OnVertex(ev, b);
    } else if (cr.type == "ellipse-curve") {
      if (!c_.allow) Unverified(cr.type);
      Cur cc = MakeCur(cr);
      cc.Header();
      const ON_3dPoint center = cc.Pt();
      ON_3dVector normal = cc.Vec();
      ON_3dVector major = cc.VecScaled();
      const double ratio = cc.Real();
      cc.Ranges(2);
      cc.End();
      if (!Unit(normal)) cc.Fail("zero normal");
      const double a = major.Length();
      if (!(a > 0.0) || !(ratio > 0.0)) cc.Fail("degenerate ellipse");
      ON_3dVector mhat = major;
      Unit(mhat);
      ON_3dVector nh = ON_CrossProduct(normal, mhat);
      const ON_3dVector minor = nh * (a * ratio);
      auto at = [&](double t) { return center + major * std::cos(t) + minor * std::sin(t); };
      double span = t1 - t0;
      if (span < 0.0) span += kTwoPi;
      const bool full = span >= kTwoPi - 1e-9 || span < 1e-12;
      std::unique_ptr<ON_NurbsCurve> nc;
      ON_3dPoint P0, P1;
      if (full) {
        double ts = t0;
        if (have_s) {
          const ON_3dVector d = sp - center;
          const double x = (d * mhat) / a, y = (d * nh) / (a * ratio);
          ts = std::atan2(y, x);
          if (at(ts).DistanceTo(sp) > kTol) {
            Refuse("edge " + std::to_string(idx) + ": start vertex does not lie on its ellipse-curve");
          }
        }
        nc.reset(MakeEllipseArc(center, major, minor, normal, ts, ts + kTwoPi));
        if (nc && revsense) nc->Reverse();
        P0 = P1 = at(ts);
      } else {
        const ON_3dPoint A = at(t0), B = at(t1);
        bool dec = revsense;
        if (have_s) {
          if (sp.DistanceTo(A) < kTol) dec = false;
          else if (sp.DistanceTo(B) < kTol) dec = true;
          else Refuse("edge " + std::to_string(idx) + ": start vertex does not match either end of its ellipse arc");
        } else if (have_e) {
          if (ep.DistanceTo(B) < kTol) dec = false;
          else if (ep.DistanceTo(A) < kTol) dec = true;
          else Refuse("edge " + std::to_string(idx) + ": end vertex does not match either end of its ellipse arc");
        }
        double tt1 = t0 + span;
        nc.reset(MakeEllipseArc(center, major, minor, normal, t0, tt1));
        if (nc && dec) nc->Reverse();
        P0 = dec ? B : A;
        P1 = dec ? A : B;
        if (have_s && sp.DistanceTo(P0) > kTol) Refuse("edge " + std::to_string(idx) + ": start vertex mismatch");
        if (have_e && ep.DistanceTo(P1) > kTol) Refuse("edge " + std::to_string(idx) + ": end vertex mismatch");
      }
      if (!nc) Refuse("edge " + std::to_string(idx) + ": could not build the ellipse arc");
      info.c3 = std::move(nc);
      info.v0 = OnVertex(sv, P0);
      info.v1 = (full && sv < 0 && ev < 0) ? info.v0 : OnVertex(ev, P1);
      if (full && sv >= 0 && ev >= 0 && sv == ev) info.v1 = info.v0;
      info.is_circle = std::fabs(ratio - 1.0) < 1e-12;
      info.center = center;
      info.normal = normal;
      info.radius = a;
    } else {
      Refuse("edge " + std::to_string(idx) + " lies on unsupported curve type '" + cr.type + "'");
    }
    return edges_.emplace(idx, std::move(info)).first->second;
  }

  void EnsureOnEdge(EdgeInfo& e) {
    if (e.on_edge >= 0) return;
    const int ci = brep_.AddEdgeCurve(e.c3->DuplicateCurve());
    ON_BrepEdge& be = brep_.NewEdge(brep_.m_V[e.v0], brep_.m_V[e.v1], ci);
    be.m_tolerance = 0.0;
    e.on_edge = be.m_edge_index;
  }

  ON_3dPoint EvalTraversal(const EdgeInfo& e, bool rev, double s) const {
    const ON_Interval d = e.c3->Domain();
    return e.c3->PointAt(d.ParameterAt(rev ? 1.0 - s : s));
  }

  std::unique_ptr<ON_Curve> TraversalCurve(const EdgeInfo& e, bool rev) const {
    std::unique_ptr<ON_Curve> c(e.c3->DuplicateCurve());
    if (rev) c->Reverse();
    return c;
  }

  // Walks a face's loop ring, returning each loop's coedge list.
  std::vector<std::vector<CoedgeRef>> LoopsOf(int first_loop) {
    std::vector<std::vector<CoedgeRef>> loops;
    std::set<int> seen_loops;
    for (int l = first_loop; l >= 0;) {
      if (!seen_loops.insert(l).second) Refuse("loop chain does not terminate (loops back to loop record " + std::to_string(l) + ")");
      const Rec& lr = Get(l, "loop");
      Cur lc = MakeCur(lr);
      lc.Header();
      const int next_loop = lc.Ptr();
      const int first_ce = lc.Ptr();
      lc.Ptr();  // face
      lc.End();
      if (first_ce < 0) Refuse("loop " + std::to_string(l) + " has no coedge");
      std::vector<CoedgeRef> ring;
      std::set<int> seen_ce;
      for (int ce = first_ce;;) {
        if (!seen_ce.insert(ce).second) Refuse("coedge ring of loop " + std::to_string(l) + " does not close (revisits coedge record " + std::to_string(ce) + ")");
        const Rec& cr = Get(ce, "coedge");
        Cur cc = MakeCur(cr);
        cc.Header();
        const int next_ce = cc.Ptr();
        cc.Ptr();  // previous
        cc.Ptr();  // partner
        const int edge = cc.Ptr();
        const std::string sense = cc.Id("forward", "reversed");
        cc.Ptr();  // owner
        if (cc.More()) cc.Ptr();  // pcurve
        cc.End();
        if (sense != "forward" && sense != "reversed") cc.Fail("bad sense '" + sense + "'");
        if (edge < 0) cc.Fail("coedge without edge");
        ring.push_back({edge, sense == "reversed"});
        if (next_ce < 0) cc.Fail("coedge ring is not closed");
        ce = next_ce;
        if (ce == first_ce) break;
      }
      loops.push_back(std::move(ring));
      l = next_loop;
    }
    return loops;
  }

  void Emit(ON_Surface* srf, bool brev, std::vector<LoopSpec>& loops) {
    const int si = brep_.AddSurface(srf);
    ON_BrepFace& face = brep_.NewFace(si);
    const int fi = face.m_face_index;
    brep_.m_F[fi].m_bRev = brev;
    for (LoopSpec& ls : loops) {
      ON_BrepLoop& loop = brep_.NewLoop(ls.outer ? ON_BrepLoop::outer : ON_BrepLoop::inner, brep_.m_F[fi]);
      const int li = loop.m_loop_index;
      for (TrimSpec& ts : ls.trims) {
        int edge_on = -1;
        if (ts.seam) {
          edge_on = ts.seam_edge;
        } else {
          EdgeInfo& e = GetEdge(ts.edge);
          EnsureOnEdge(e);
          edge_on = e.on_edge;
        }
        const int c2i = brep_.AddTrimCurve(ts.c2.release());
        ON_BrepTrim& trim = brep_.NewTrim(brep_.m_E[edge_on], ts.rev, brep_.m_L[li], c2i);
        trim.m_tolerance[0] = trim.m_tolerance[1] = 0.0;
        if (ts.seam) trim.m_type = ON_BrepTrim::seam;
      }
    }
  }

  // Returns the next face in the shell's chain.
  int BuildFace(int fidx) {
    const Rec& fr = Get(fidx, "face");
    Cur c = MakeCur(fr);
    c.Header();
    const int next = c.Ptr();
    const int first_loop = c.Ptr();
    c.Ptr();  // shell
    const int subshell = c.Ptr();
    const int surf = c.Ptr();
    const std::string sense = c.Id("forward", "reversed");
    const std::string sides = c.Id("single", "double");
    if (sides == "double") c.Id("in", "out");  // containment in/out
    else if (sides != "single") c.Fail("bad sides '" + sides + "'");
    c.End();
    if (sense != "forward" && sense != "reversed") c.Fail("bad sense '" + sense + "'");
    if (subshell >= 0) c.Fail("subshell reference (not supported)");
    if (surf < 0) c.Fail("face without surface");
    const bool face_reversed = (sense == "reversed");
    const Rec& sr = GetAny(surf);
    const std::string where = "face " + std::to_string(fidx) + ": ";
    try {
      if (sr.type == "cone-surface" || sr.type == "sphere-surface" || sr.type == "torus-surface") {
        if (!c_.allow) Unverified(sr.type);
      }
      if (sr.type == "plane-surface") BuildPlaneFace(fidx, sr, first_loop, face_reversed);
      else if (sr.type == "cone-surface") BuildConeFace(fidx, sr, first_loop, face_reversed);
      else if (sr.type == "sphere-surface" || sr.type == "torus-surface") BuildClosedQuadricFace(fidx, sr, first_loop, face_reversed);
      else Refuse("unsupported surface type '" + sr.type + "'");
    } catch (const SatError& e) {
      const std::string m = e.what();
      if (m.rfind("face ", 0) == 0) throw;
      throw SatError(where + m);
    }
    return next;
  }

  void WarnOrientation(int fidx, bool brev, bool face_reversed_expected) {
    if (brev != face_reversed_expected && c_.warnings) {
      c_.warnings->push_back("face " + std::to_string(fidx) +
                             ": stated sense disagrees with the loop winding; orientation taken from the loops");
    }
  }

  // ---- plane ----
  void BuildPlaneFace(int fidx, const Rec& sr, int first_loop, bool face_reversed) {
    Cur c = MakeCur(sr);
    c.Header();
    const ON_3dPoint O = c.Pt();
    ON_3dVector N = c.Vec();
    ON_3dVector U = c.Vec();
    const std::string vsense = c.Id("forward_v", "reverse_v");
    c.Ranges(4);
    c.End();
    (void)vsense;
    if (!Unit(N)) c.Fail("zero normal");
    U = U - (U * N) * N;
    if (!Unit(U)) c.Fail("u direction parallel to the normal");
    const ON_3dVector V = ON_CrossProduct(N, U);
    if (first_loop < 0) Refuse("planar face without a loop is not supported");
    auto loops = LoopsOf(first_loop);

    ON_Xform xf(ON_Xform::IdentityTransformation);
    for (int i = 0; i < 3; ++i) {
      xf.m_xform[0][i] = U[i];
      xf.m_xform[1][i] = V[i];
      xf.m_xform[2][i] = N[i];
    }
    xf.m_xform[0][3] = -(U * O);
    xf.m_xform[1][3] = -(V * O);
    xf.m_xform[2][3] = -(N * O);

    std::vector<LoopSpec> specs;
    std::vector<double> areas;
    double umin = 1e300, umax = -1e300, vmin = 1e300, vmax = -1e300;
    for (const auto& ring : loops) {
      LoopSpec ls;
      std::vector<ON_2dPoint> poly;
      for (const CoedgeRef& cr : ring) {
        EdgeInfo& e = GetEdge(cr.edge);
        std::unique_ptr<ON_Curve> c2(TraversalCurve(e, cr.rev));
        c2->Transform(xf);
        for (int k = 0; k <= 8; ++k) {
          const ON_3dPoint q = c2->PointAt(c2->Domain().ParameterAt(k / 8.0));
          if (std::fabs(q.z) > kTol) Refuse("an edge does not lie on its face's plane (distance " + Num(q.z) + ")");
        }
        if (!c2->ChangeDimension(2)) Refuse("could not project an edge into the plane's parameter space");
        for (int k = 0; k < 8; ++k) {
          const ON_3dPoint q = c2->PointAt(c2->Domain().ParameterAt(k / 8.0));
          poly.emplace_back(q.x, q.y);
          umin = std::min(umin, q.x);
          umax = std::max(umax, q.x);
          vmin = std::min(vmin, q.y);
          vmax = std::max(vmax, q.y);
        }
        TrimSpec ts;
        ts.edge = cr.edge;
        ts.rev = cr.rev;
        ts.c2 = std::move(c2);
        ls.trims.push_back(std::move(ts));
      }
      areas.push_back(SignedAreaUV(poly));
      specs.push_back(std::move(ls));
    }
    size_t outer = 0;
    for (size_t i = 1; i < areas.size(); ++i) {
      if (std::fabs(areas[i]) > std::fabs(areas[outer])) outer = i;
    }
    if (std::fabs(areas[outer]) < 1e-14) Refuse("planar face with zero area");
    for (size_t i = 0; i < areas.size(); ++i) {
      if (i != outer && areas[i] * areas[outer] > 0.0) Refuse("an inner loop winds the same way as the outer loop");
      specs[i].outer = (i == outer);
    }
    std::swap(specs[0], specs[outer]);
    const bool brev = areas[outer] < 0.0;
    WarnOrientation(fidx, brev, face_reversed);
    const double padu = 0.1 * std::max(1.0, umax - umin), padv = 0.1 * std::max(1.0, vmax - vmin);
    ON_NurbsSurface* srf = MakePlaneNurbs(O, U, V, umin - padu, umax + padu, vmin - padv, vmax + padv);
    Emit(srf, brev, specs);
  }

  // ---- cone / cylinder ----
  struct Sample {
    double z, rho, theta;
  };

  void BuildConeFace(int fidx, const Rec& sr, int first_loop, bool face_reversed) {
    Cur c = MakeCur(sr);
    c.Header();
    const ON_3dPoint C = c.Pt();
    ON_3dVector A = c.Vec();
    ON_3dVector M = c.VecScaled();
    const double ratio = c.Real();
    const double sinA = c.Real();
    const double cosA = c.Real();
    if (c.PeekNum()) c.Real();  // u-parameter scale in some versions
    c.Id("forward_v", "reverse_v");
    c.Ranges(4);
    c.End();
    if (!Unit(A)) c.Fail("zero axis");
    const double Rc = M.Length();
    if (!(Rc > 0.0)) c.Fail("zero radius");
    if (std::fabs(ratio - 1.0) > 1e-9) Refuse("elliptical cone/cylinder (ratio != 1) is not supported");
    if (!(cosA > 1e-9)) c.Fail("bad half-angle");
    ON_3dVector E1 = M - (M * A) * A;
    if (!Unit(E1)) c.Fail("major axis parallel to the axis");
    const ON_3dVector E2 = ON_CrossProduct(A, E1);
    const double slope = sinA / cosA;
    if (first_loop < 0) Refuse("cone/cylinder face without a loop is not supported");
    auto loops = LoopsOf(first_loop);

    struct LoopSamples {
      std::vector<Sample> s;
      std::vector<size_t> trim_begin;  // first sample index of each coedge
    };
    std::vector<LoopSamples> ls(loops.size());
    for (size_t li = 0; li < loops.size(); ++li) {
      double prev = 0.0;
      bool first = true;
      for (const CoedgeRef& cr : loops[li]) {
        EdgeInfo& e = GetEdge(cr.edge);
        int ns = 17;
        if (e.is_line) ns = 2;
        else if (e.is_circle && std::fabs(std::fabs(e.normal * A) - 1.0) < 1e-9) {
          const ON_3dVector dc = e.center - C;
          const double off = (dc - (dc * A) * A).Length();
          if (off < kTol) ns = 5;
        }
        ls[li].trim_begin.push_back(ls[li].s.size());
        for (int k = 0; k < ns; ++k) {
          if (k == 0 && !first) {
            // shares its point with the previous coedge's last sample
          }
          const ON_3dPoint p = EvalTraversal(e, cr.rev, static_cast<double>(k) / (ns - 1));
          const ON_3dVector d = p - C;
          const double z = d * A;
          const ON_3dVector perp = d - z * A;
          double th = std::atan2(perp * E2, perp * E1);
          if (!first) {
            while (th - prev > kPi) th -= kTwoPi;
            while (th - prev < -kPi) th += kTwoPi;
          }
          first = false;
          prev = th;
          ls[li].s.push_back({z, perp.Length(), th});
        }
      }
    }
    // Radius law: r(z) = Rc + k z with k = +/- slope.
    double best_err[2] = {0.0, 0.0};
    for (int sgn = 0; sgn < 2; ++sgn) {
      const double k = (sgn == 0 ? 1.0 : -1.0) * slope;
      for (const auto& l : ls)
        for (const Sample& s : l.s) best_err[sgn] = std::max(best_err[sgn], std::fabs(s.rho - (Rc + k * s.z)));
    }
    const int pick = best_err[0] <= best_err[1] ? 0 : 1;
    if (best_err[pick] > 1e-5 * std::max(1.0, Rc)) {
      Refuse("edge samples do not lie on the face's cone/cylinder (worst radial error " + Num(best_err[pick]) + ")");
    }
    const double k = (pick == 0 ? 1.0 : -1.0) * slope;
    double zmin = 1e300, zmax = -1e300;
    for (const auto& l : ls)
      for (const Sample& s : l.s) {
        zmin = std::min(zmin, s.z);
        zmax = std::max(zmax, s.z);
        if (Rc + k * s.z < 1e-9) Refuse("cone apex lies on the face boundary (not supported)");
      }
    if (zmax - zmin < kTol) Refuse("cone/cylinder face of zero height");

    // Net winding of each loop.
    std::vector<double> net(loops.size());
    int wound = 0;
    for (size_t i = 0; i < ls.size(); ++i) {
      net[i] = ls[i].s.back().theta - ls[i].s.front().theta;
      if (std::fabs(std::fabs(net[i]) - kTwoPi) < 1e-6) ++wound;
      else if (std::fabs(net[i]) > 1e-6) Refuse("loop winds a non-integer number of turns around the axis");
    }
    if (wound == 0) {
      BuildConeGeneric(fidx, loops, ls, C, A, E1, Rc, k, zmin, zmax, face_reversed);
    } else if (wound == 2 && loops.size() == 2) {
      BuildConePeriodic(fidx, loops, ls, C, A, E1, Rc, k, zmin, zmax, net, face_reversed);
    } else {
      Refuse("unsupported periodic face layout (" + std::to_string(wound) + " winding loop(s) of " +
             std::to_string(loops.size()) + ")");
    }
  }

  template <typename LS>
  void BuildConeGeneric(int fidx, const std::vector<std::vector<CoedgeRef>>& loops, std::vector<LS>& ls,
                        const ON_3dPoint& C, const ON_3dVector& A, const ON_3dVector& E1, double Rc, double k,
                        double zmin, double zmax, bool face_reversed) {
    // Put every loop on the same 2pi branch as loop 0's first sample.
    const double ref = ls[0].s.front().theta;
    for (size_t i = 0; i < ls.size(); ++i) {
      const double d = ls[i].s.front().theta - ref;
      const double shift = -kTwoPi * std::round(d / kTwoPi);
      for (Sample& s : ls[i].s) s.theta += shift;
    }
    double thmin = 1e300, thmax = -1e300;
    for (const auto& l : ls)
      for (const Sample& s : l.s) {
        thmin = std::min(thmin, s.theta);
        thmax = std::max(thmax, s.theta);
      }
    if (thmax - thmin > kTwoPi - 1e-6) Refuse("face spans the whole circumference but has no winding loop (not supported)");
    const double a0 = thmin;
    std::vector<LoopSpec> specs;
    std::vector<double> areas;
    for (size_t li = 0; li < loops.size(); ++li) {
      LoopSpec lsp;
      std::vector<ON_2dPoint> poly;
      for (size_t ci = 0; ci < loops[li].size(); ++ci) {
        const size_t b = ls[li].trim_begin[ci];
        const size_t e = (ci + 1 < loops[li].size()) ? ls[li].trim_begin[ci + 1] : ls[li].s.size();
        std::vector<ON_3dPoint> pts;
        for (size_t q = b; q < e; ++q) pts.emplace_back(UOfPhi(ls[li].s[q].theta - a0), ls[li].s[q].z, 0.0);
        std::unique_ptr<ON_Curve> c2;
        if (pts.size() <= 5) {
          c2.reset(new ON_LineCurve(ON_3dPoint(pts.front().x, pts.front().y, 0.0), ON_3dPoint(pts.back().x, pts.back().y, 0.0)));
        } else {
          auto* pl = new ON_PolylineCurve();
          for (const auto& p : pts) pl->m_pline.Append(ON_3dPoint(p.x, p.y, 0.0));
          pl->SetDomain(0.0, 1.0);
          c2.reset(pl);
        }
        c2->ChangeDimension(2);
        for (size_t q = b; q < e; ++q) {
          if (q + 1 < e || ci + 1 == loops[li].size()) poly.emplace_back(UOfPhi(ls[li].s[q].theta - a0), ls[li].s[q].z);
        }
        TrimSpec ts;
        ts.edge = loops[li][ci].edge;
        ts.rev = loops[li][ci].rev;
        ts.c2 = std::move(c2);
        lsp.trims.push_back(std::move(ts));
      }
      // drop the duplicated closing sample
      if (poly.size() > 1) poly.pop_back();
      areas.push_back(SignedAreaUV(poly));
      specs.push_back(std::move(lsp));
    }
    size_t outer = 0;
    for (size_t i = 1; i < areas.size(); ++i) {
      if (std::fabs(areas[i]) > std::fabs(areas[outer])) outer = i;
    }
    if (std::fabs(areas[outer]) < 1e-14) Refuse("cone/cylinder face with zero parametric area");
    for (size_t i = 0; i < areas.size(); ++i) {
      if (i != outer && areas[i] * areas[outer] > 0.0) Refuse("an inner loop winds the same way as the outer loop");
      specs[i].outer = (i == outer);
    }
    std::swap(specs[0], specs[outer]);
    const bool brev = areas[outer] < 0.0;
    WarnOrientation(fidx, brev, face_reversed);
    Emit(MakeConeNurbs(C, A, E1, Rc, k, a0, zmin, zmax), brev, specs);
  }

  template <typename LS>
  void BuildConePeriodic(int fidx, const std::vector<std::vector<CoedgeRef>>& loops, std::vector<LS>& ls,
                         const ON_3dPoint& C, const ON_3dVector& A, const ON_3dVector& E1, double Rc, double k,
                         double zmin, double zmax, const std::vector<double>& net, bool face_reversed) {
    // Identify bottom / top circle loops: each must sit at one height.
    int bot = -1, top = -1;
    double zl[2];
    for (int i = 0; i < 2; ++i) {
      double lo = 1e300, hi = -1e300;
      for (const Sample& s : ls[static_cast<size_t>(i)].s) {
        lo = std::min(lo, s.z);
        hi = std::max(hi, s.z);
      }
      if (hi - lo > kTol) Refuse("a winding loop is not a circle about the axis");
      zl[i] = 0.5 * (lo + hi);
    }
    if (std::fabs(zl[0] - zl[1]) < kTol) Refuse("both winding loops sit at the same height");
    bot = zl[0] < zl[1] ? 0 : 1;
    top = 1 - bot;
    const double db = net[static_cast<size_t>(bot)] > 0 ? 1.0 : -1.0;
    const double dt = net[static_cast<size_t>(top)] > 0 ? 1.0 : -1.0;
    if (db * dt > 0.0) Refuse("the two boundary circles of a periodic face run the same way (inconsistent loop orientation)");
    const double th_b = ls[static_cast<size_t>(bot)].s.front().theta;
    const double th_t = ls[static_cast<size_t>(top)].s.front().theta;
    const double dd = (th_t - th_b) / kTwoPi;
    if (std::fabs(dd - std::round(dd)) * kTwoPi > 1e-6) {
      Refuse("seam alignment: the two boundary circles start at different angles (needs an edge split, not supported)");
    }
    const double a0 = th_b;
    const bool brev = db < 0.0;
    WarnOrientation(fidx, brev, face_reversed);

    auto loop_vertex = [&](int li) {
      const CoedgeRef& cr = loops[static_cast<size_t>(li)].front();
      EdgeInfo& e = GetEdge(cr.edge);
      EnsureOnEdge(e);
      return cr.rev ? e.v1 : e.v0;
    };
    const int vb = loop_vertex(bot), vt = loop_vertex(top);
    // Seam edge (bottom start vertex -> top start vertex).
    const int sci = brep_.AddEdgeCurve(new ON_LineCurve(brep_.m_V[vb].point, brep_.m_V[vt].point));
    ON_BrepEdge& se = brep_.NewEdge(brep_.m_V[vb], brep_.m_V[vt], sci);
    se.m_tolerance = 0.0;
    const int seam_edge = se.m_edge_index;

    auto circle_trims = [&](int li, double start_u, double dir, LoopSpec& out) {
      const size_t L = static_cast<size_t>(li);
      const double th0 = ls[L].s.front().theta;
      for (size_t ci = 0; ci < loops[L].size(); ++ci) {
        const size_t b = ls[L].trim_begin[ci];
        const size_t e = (ci + 1 < loops[L].size()) ? ls[L].trim_begin[ci + 1] : ls[L].s.size();
        const double ua = start_u + (ls[L].s[b].theta - th0);
        const double ub = start_u + (ls[L].s[e - 1].theta - th0);
        (void)dir;
        // u here is an ANGLE; convert to the surface parameter.
        const double pa = UOfPhi(std::min(std::max(ua, 0.0), kTwoPi));
        const double pb = UOfPhi(std::min(std::max(ub, 0.0), kTwoPi));
        TrimSpec ts;
        ts.edge = loops[L][ci].edge;
        ts.rev = loops[L][ci].rev;
        const double z = ls[L].s[b].z;
        auto* lc = new ON_LineCurve(ON_3dPoint(pa, z, 0.0), ON_3dPoint(pb, z, 0.0));
        lc->ChangeDimension(2);
        ts.c2.reset(lc);
        out.trims.push_back(std::move(ts));
      }
    };
    auto seam_trim = [&](double u_param, double z0, double z1, bool rev, LoopSpec& out) {
      TrimSpec ts;
      ts.seam = true;
      ts.seam_edge = seam_edge;
      ts.rev = rev;
      auto* lc = new ON_LineCurve(ON_3dPoint(u_param, z0, 0.0), ON_3dPoint(u_param, z1, 0.0));
      lc->ChangeDimension(2);
      ts.c2.reset(lc);
      out.trims.push_back(std::move(ts));
    };
    LoopSpec lsp;
    lsp.outer = true;
    const double zb = zl[bot], zt = zl[top];
    if (db > 0.0) {
      circle_trims(bot, 0.0, 1.0, lsp);
      seam_trim(4.0, zb, zt, false, lsp);
      circle_trims(top, kTwoPi, -1.0, lsp);
      seam_trim(0.0, zt, zb, true, lsp);
    } else {
      circle_trims(bot, kTwoPi, -1.0, lsp);
      seam_trim(0.0, zb, zt, false, lsp);
      circle_trims(top, 0.0, 1.0, lsp);
      seam_trim(4.0, zt, zb, true, lsp);
    }
    std::vector<LoopSpec> specs;
    specs.push_back(std::move(lsp));
    Emit(MakeConeNurbs(C, A, E1, Rc, k, a0, zmin, zmax), brev, specs);
  }

  // ---- sphere / torus (whole-surface faces only) ----
  void BuildClosedQuadricFace(int fidx, const Rec& sr, int first_loop, bool face_reversed) {
    if (first_loop >= 0) Refuse("a loop on a " + sr.type + " (partial sphere/torus faces) is not supported");
    if (brep_.m_F.Count() != 0) Refuse("a whole sphere/torus face combined with other faces is not supported");
    Cur c = MakeCur(sr);
    c.Header();
    const ON_3dPoint C = c.Pt();
    // The Brep is built through the kernel's own whole-surface factories so
    // its per-face side tables stay consistent (the sphere/torus frame - pole
    // and seam direction - is therefore the factory's, not the file's).
    if (sr.type == "sphere-surface") {
      const double radius = c.Real() * c.scale;
      if (c.PeekNum()) {
        c.Vec();
        c.Vec();
      }
      c.Id("forward_v", "reverse_v");
      c.Ranges(4);
      c.End();
      if (!(radius > 0.0)) Refuse("sphere with non-positive radius is not supported");
      out_ = Brep::Sphere(C, radius);
    } else {
      ON_3dVector axis = c.Vec();
      const double major = c.Real() * c.scale;
      const double minor = c.Real() * c.scale;
      if (c.PeekNum()) c.Vec();
      c.Id("forward_v", "reverse_v");
      c.Ranges(4);
      c.End();
      if (!Unit(axis)) c.Fail("zero axis");
      if (!(minor > 0.0) || !(major > minor)) Refuse("only ring tori (major > minor > 0) are supported");
      out_ = Brep::Torus(C, axis, major, minor);
    }
    if (out_.raw().m_F.Count() != 1) Refuse("internal: whole-surface factory did not build one face");
    out_.raw().m_F[0].m_bRev = face_reversed;
    any_loopless_ = true;
    whole_quadric_ = true;
    (void)fidx;
  }

  Ctx& c_;
  Brep out_;
  ON_Brep& brep_;
  std::map<int, int> vertex_of_;
  std::map<int, EdgeInfo> edges_;
  bool any_loopless_ = false;
  bool whole_quadric_ = false;
};

}  // namespace

namespace {

enum class Encoding { Sat, Sab };

// Shared by every entry point: bytes -> tokens -> records -> bodies.
AcisSatReadResult ReadBytes(const std::string& bytes, Encoding enc, const AcisSatOptions& options) {
  AcisSatReadResult res;
  ParsedFile pf;
  try {
    pf = (enc == Encoding::Sab) ? ParseSab(bytes) : ParseSat(bytes);
  } catch (const SatError& e) {
    res.error = e.what();
    return res;
  } catch (const std::exception& e) {
    res.error = std::string("internal error while parsing: ") + e.what();
    return res;
  }
  for (const std::string& n : pf.notes) res.warnings.push_back(n);
  Ctx ctx{pf};
  ctx.scale = pf.units;
  ctx.warnings = &res.warnings;
  ctx.allow = options.allow_unverified_layouts;
  ctx.max_visits = 64LL * static_cast<long long>(pf.recs.size()) + 100000LL;
  ctx.h = (pf.version >= 700) ? 3 : 1;
  for (const Rec& r : pf.recs) {
    if (r.type == "point") {
      if (r.f.size() == 4) ctx.h = 1;
      else if (r.f.size() == 5) ctx.h = 2;
      else if (r.f.size() == 6) ctx.h = 3;
      break;
    }
  }
  std::vector<int> bodies;
  for (const Rec& r : pf.recs) {
    if (r.type == "body") bodies.push_back(r.index);
  }
  if (bodies.empty()) {
    res.error = "no body entity in the file";
    return res;
  }
  std::vector<std::string> reasons;
  for (int b : bodies) {
    try {
      BodyBuilder bb(ctx);
      res.bodies.push_back(bb.Build(b));
    } catch (const SatError& e) {
      reasons.push_back("body " + std::to_string(b) + " refused: " + e.what());
    } catch (const std::bad_alloc&) {
      reasons.push_back("body " + std::to_string(b) + " refused: out of memory while building it");
    } catch (const std::exception& e) {
      reasons.push_back("body " + std::to_string(b) + " refused (internal error): " + e.what());
    }
  }
  for (const std::string& m : reasons) res.warnings.push_back(m);
  if (res.bodies.empty()) {
    res.error = reasons.empty() ? "no body could be converted" : reasons.front();
    return res;
  }
  res.ok = true;
  return res;
}

AcisSatReadResult ReadStream(std::istream& in, int mode, const AcisSatOptions& options) {  // mode: 0 SAT, 1 SAB, 2 auto
  std::string bytes;
  try {
    bytes = ReadAllBounded(in);
  } catch (const SatError& e) {
    AcisSatReadResult r;
    r.error = e.what();
    return r;
  } catch (const std::exception& e) {
    AcisSatReadResult r;
    r.error = std::string("cannot read the input: ") + e.what();
    return r;
  }
  Encoding enc = (mode == 1) ? Encoding::Sab : Encoding::Sat;
  if (mode == 2) enc = LooksLikeAcisSab(bytes) ? Encoding::Sab : Encoding::Sat;
  return ReadBytes(bytes, enc, options);
}

AcisSatReadResult ReadPath(const std::string& path, int mode, const AcisSatOptions& options) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    AcisSatReadResult r;
    r.error = "cannot open '" + path + "'";
    return r;
  }
  return ReadStream(f, mode, options);
}

}  // namespace

bool LooksLikeAcisSab(const std::string& bytes) {
  return bytes.size() >= kSabMagicLen && std::memcmp(bytes.data(), kSabMagic, kSabMagicLen) == 0;
}

AcisSatReadResult ReadAcisSat(std::istream& in, const AcisSatOptions& options) { return ReadStream(in, 0, options); }
AcisSatReadResult ReadAcisSat(const std::string& path, const AcisSatOptions& options) { return ReadPath(path, 0, options); }
AcisSatReadResult ReadAcisSab(std::istream& in, const AcisSatOptions& options) { return ReadStream(in, 1, options); }
AcisSatReadResult ReadAcisSab(const std::string& path, const AcisSatOptions& options) { return ReadPath(path, 1, options); }
AcisSatReadResult ReadAcis(std::istream& in, const AcisSatOptions& options) { return ReadStream(in, 2, options); }
AcisSatReadResult ReadAcis(const std::string& path, const AcisSatOptions& options) { return ReadPath(path, 2, options); }

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

namespace {

struct WChain {
  std::vector<int> trims;  // ON trim indices, in loop order
};

// One output token / record, encoding-neutral: the text writer prints it, the
// binary writer packs it. Idents are the two-valued words (and "I"), see kSabBool.
struct WTok {
  enum K { Ptr, Int, Real, Ident, Str, Pos, Vec } k = Int;
  long long i = 0;
  double d[3] = {0.0, 0.0, 0.0};
  std::string s;
};
struct WRec {
  std::string type;
  std::vector<WTok> f;
  WRec& Ptr(int v) { WTok t; t.k = WTok::Ptr; t.i = v; f.push_back(t); return *this; }
  WRec& Int(long long v) { WTok t; t.k = WTok::Int; t.i = v; f.push_back(t); return *this; }
  WRec& Real(double v) { WTok t; t.k = WTok::Real; t.d[0] = v; f.push_back(t); return *this; }
  WRec& Id(const char* w) { WTok t; t.k = WTok::Ident; t.s = w; f.push_back(t); return *this; }
  WRec& Str(const std::string& w) { WTok t; t.k = WTok::Str; t.s = w; f.push_back(t); return *this; }
  WRec& Pos(const ON_3dPoint& p) { WTok t; t.k = WTok::Pos; t.d[0] = p.x; t.d[1] = p.y; t.d[2] = p.z; f.push_back(t); return *this; }
  WRec& Vec(const ON_3dVector& v) { WTok t; t.k = WTok::Vec; t.d[0] = v.x; t.d[1] = v.y; t.d[2] = v.z; f.push_back(t); return *this; }
  WRec& Hdr() { return Ptr(-1).Int(-1).Ptr(-1); }  // attribute, id, history (UNVERIFIED third item)
  WRec& Ranges(int n) { for (int k = 0; k < n; ++k) Id("I"); return *this; }
};

struct WFace {
  int on_face = -1;
  std::vector<WChain> chains;
  int surf_kind = 0;  // 1 plane, 2 cylinder, 3 sphere, 4 torus
  WRec surf;  // type + everything after the header triple
  bool reversed = false;
};

[[noreturn]] void WRefuse(const std::string& m) { throw SatError(m); }


class BodyWriter {
 public:
  BodyWriter(const ON_Brep& b, std::vector<std::string>& warnings, bool allow) : b_(b), warnings_(warnings), allow_(allow) {}

  // Appends this body's records; their indices start at recs.size().
  void Write(std::vector<WRec>& recs) {
    Classify();
    Emit(recs);
  }

 private:
  struct WEdge {
    int on_edge = -1;
    int sv = -1, ev = -1;   // ON vertex ids
    WRec curve;  // type + everything after the header triple
    double t0 = 0.0, t1 = 0.0;
    bool reversed = false;
    int first_coedge = -1;  // index into coedge list
  };
  struct WCoedge {
    int trim = -1;
    int edge = -1;  // index into edges_
    bool rev = false;
    int loop = -1;
    int next = -1, prev = -1, partner = -1;
  };
  struct WLoop {
    int face = -1;
    std::vector<int> coedges;
  };

  void Classify() {
    const double tol = kTol;
    const int nf = b_.m_F.Count();
    for (int fi = 0; fi < nf; ++fi) {
      const ON_BrepFace& face = b_.m_F[fi];
      if (face.m_face_index < 0) continue;
      WFace wf;
      wf.on_face = fi;
      const std::string fname = "face " + std::to_string(fi);
      // chains
      for (int k = 0; k < face.m_li.Count(); ++k) {
        const ON_BrepLoop& loop = b_.m_L[face.m_li[k]];
        std::vector<int> ts;
        for (int t = 0; t < loop.m_ti.Count(); ++t) {
          const ON_BrepTrim& trim = b_.m_T[loop.m_ti[t]];
          ts.push_back(loop.m_ti[t]);
        }
        std::vector<bool> is_seam(ts.size());
        bool any_seam = false;
        for (size_t t = 0; t < ts.size(); ++t) {
          is_seam[t] = (b_.m_T[ts[t]].m_type == ON_BrepTrim::seam || b_.m_T[ts[t]].m_type == ON_BrepTrim::singular);
          any_seam = any_seam || is_seam[t];
        }
        if (!any_seam) {
          WChain ch;
          ch.trims = ts;
          wf.chains.push_back(std::move(ch));
          continue;
        }
        // rotate to start right after a seam, then cut at seams
        size_t start = 0;
        for (size_t t = 0; t < ts.size(); ++t)
          if (is_seam[t]) {
            start = (t + 1) % ts.size();
            break;
          }
        WChain cur;
        for (size_t q = 0; q < ts.size(); ++q) {
          const size_t t = (start + q) % ts.size();
          if (is_seam[t]) {
            if (!cur.trims.empty()) wf.chains.push_back(std::move(cur));
            cur = WChain();
          } else {
            cur.trims.push_back(ts[t]);
          }
        }
        if (!cur.trims.empty()) wf.chains.push_back(std::move(cur));
        for (const WChain& ch : wf.chains) {
          const ON_BrepTrim& a = b_.m_T[ch.trims.front()];
          const ON_BrepTrim& z = b_.m_T[ch.trims.back()];
          if (a.m_vi[0] != z.m_vi[1]) WRefuse(fname + ": seam topology that does not reduce to closed boundary loops");
        }
      }
      // surface
      const ON_Surface* srf = face.SurfaceOf();
      if (!srf) WRefuse(fname + " has no surface");
      const ON_Interval du = srf->Domain(0), dv = srf->Domain(1);
      const double um = du.ParameterAt(0.5), vm = dv.ParameterAt(0.5);
      ON_3dVector sn = srf->NormalAt(um, vm);
      const ON_3dPoint sp = srf->PointAt(um, vm);
      const ON_3dVector fn = face.m_bRev ? -sn : sn;
      ON_Plane pl;
      ON_Cylinder cyl;
      ON_Sphere sph;
      ON_Torus tor;
      if (srf->IsPlanar(&pl, tol)) {
        if (wf.chains.empty()) WRefuse(fname + ": planar face without boundary loops");
        wf.surf.type = "plane-surface";
        wf.surf.Hdr().Pos(pl.origin).Vec(pl.zaxis).Vec(pl.xaxis).Id("forward_v").Ranges(4);
        wf.reversed = (fn * pl.zaxis) < 0.0;
      } else if (srf->IsCylinder(&cyl, tol)) {
        NeedUnverified(fname, "cone-surface");
        const ON_3dVector ax = cyl.circle.plane.zaxis;
        const ON_3dVector d = sp - cyl.circle.plane.origin;
        const ON_3dVector rad = d - (d * ax) * ax;
        wf.surf.type = "cone-surface";
        wf.surf.Hdr().Pos(cyl.circle.plane.origin).Vec(ax).Vec(cyl.circle.plane.xaxis * cyl.circle.radius);
        wf.surf.Real(1).Real(0).Real(1).Real(1).Id("forward_v").Ranges(4);
        wf.reversed = (fn * rad) < 0.0;
      } else if (srf->IsSphere(&sph, tol)) {
        NeedUnverified(fname, "sphere-surface");
        if (!wf.chains.empty()) WRefuse(fname + ": a trimmed sphere face is not supported");
        wf.surf.type = "sphere-surface";
        wf.surf.Hdr().Pos(sph.plane.origin).Real(sph.radius).Vec(sph.plane.xaxis).Vec(sph.plane.zaxis).Id("forward_v").Ranges(4);
        wf.reversed = (fn * (sp - sph.plane.origin)) < 0.0;
      } else if (srf->IsTorus(&tor, tol)) {
        NeedUnverified(fname, "torus-surface");
        if (!wf.chains.empty()) WRefuse(fname + ": a trimmed torus face is not supported");
        const ON_3dVector d = sp - tor.plane.origin;
        const ON_3dVector rad = d - (d * tor.plane.zaxis) * tor.plane.zaxis;
        ON_3dVector rh = rad;
        rh.Unitize();
        const ON_3dPoint tube = tor.plane.origin + rh * tor.major_radius;
        wf.surf.type = "torus-surface";
        wf.surf.Hdr().Pos(tor.plane.origin).Vec(tor.plane.zaxis).Real(tor.major_radius).Real(tor.minor_radius).Vec(tor.plane.xaxis);
        wf.surf.Id("forward_v").Ranges(4);
        wf.reversed = (fn * (sp - tube)) < 0.0;
      } else {
        WRefuse(fname + ": surface is not a plane, cylinder, sphere or torus (NURBS/other surfaces are not supported in this round)");
      }
      if (wf.chains.empty()) any_loopless_ = true;
      faces_.push_back(std::move(wf));
    }
    if (faces_.empty()) WRefuse("body has no faces");

    // edges used by kept trims
    std::map<int, int> edge_slot;
    for (size_t f = 0; f < faces_.size(); ++f) {
      for (const WChain& ch : faces_[f].chains) {
        WLoop lp;
        lp.face = static_cast<int>(f);
        const int loop_id = static_cast<int>(loops_.size());
        for (int ti : ch.trims) {
          const ON_BrepTrim& trim = b_.m_T[ti];
          const int ei = trim.m_ei;
          if (ei < 0) WRefuse("trim without an edge");
          auto it = edge_slot.find(ei);
          if (it == edge_slot.end()) {
            it = edge_slot.emplace(ei, static_cast<int>(edges_.size())).first;
            edges_.push_back(MakeEdge(ei));
          }
          WCoedge ce;
          ce.trim = ti;
          ce.edge = it->second;
          ce.rev = trim.m_bRev3d;
          ce.loop = loop_id;
          const int ci = static_cast<int>(coedges_.size());
          lp.coedges.push_back(ci);
          if (edges_[static_cast<size_t>(ce.edge)].first_coedge < 0) edges_[static_cast<size_t>(ce.edge)].first_coedge = ci;
          coedges_.push_back(ce);
        }
        const size_t m = lp.coedges.size();
        for (size_t q = 0; q < m; ++q) {
          coedges_[static_cast<size_t>(lp.coedges[q])].next = lp.coedges[(q + 1) % m];
          coedges_[static_cast<size_t>(lp.coedges[q])].prev = lp.coedges[(q + m - 1) % m];
        }
        loops_.push_back(std::move(lp));
      }
    }
    // partners: ring of coedges around each edge
    std::map<int, std::vector<int>> by_edge;
    for (size_t i = 0; i < coedges_.size(); ++i) by_edge[coedges_[i].edge].push_back(static_cast<int>(i));
    for (auto& kv : by_edge) {
      const auto& v = kv.second;
      for (size_t q = 0; q < v.size(); ++q) coedges_[static_cast<size_t>(v[q])].partner = (v.size() > 1) ? v[(q + 1) % v.size()] : -1;
    }
    // vertices
    for (const WEdge& e : edges_) {
      for (int v : {e.sv, e.ev})
        if (vertex_slot_.find(v) == vertex_slot_.end()) {
          vertex_slot_[v] = static_cast<int>(vertices_.size());
          vertices_.push_back(v);
        }
    }
    // shells: connected components of faces through shared edges
    std::vector<int> comp(faces_.size());
    for (size_t i = 0; i < comp.size(); ++i) comp[i] = static_cast<int>(i);
    std::function<int(int)> find = [&](int x) { return comp[static_cast<size_t>(x)] == x ? x : comp[static_cast<size_t>(x)] = find(comp[static_cast<size_t>(x)]); };
    std::map<int, int> edge_face;
    for (const WCoedge& ce : coedges_) {
      const int f = loops_[static_cast<size_t>(ce.loop)].face;
      auto it = edge_face.find(ce.edge);
      if (it == edge_face.end()) edge_face[ce.edge] = f;
      else comp[static_cast<size_t>(find(f))] = find(it->second);
    }
    std::map<int, int> shell_of_root;
    for (size_t f = 0; f < faces_.size(); ++f) {
      const int r = find(static_cast<int>(f));
      auto it = shell_of_root.find(r);
      if (it == shell_of_root.end()) {
        it = shell_of_root.emplace(r, static_cast<int>(shells_.size())).first;
        shells_.emplace_back();
      }
      shells_[static_cast<size_t>(it->second)].push_back(static_cast<int>(f));
    }
    // solid or sheet?
    solid_ = b_.IsSolid() || (any_loopless_ && edges_.empty());
    if (!solid_ && warnings_.size() < 1000) {
      // Open (or topologically unverified) body: faces are written double-sided.
    }
  }

  void NeedUnverified(const std::string& where, const char* type) const {
    if (!allow_) {
      WRefuse(where + " needs '" + type +
              "', whose field layout is not stated in the public documentation used (UNVERIFIED, see "
              "docs/ACIS_SAT_NOTES.md); set AcisSatOptions::allow_unverified_layouts to write it anyway");
    }
  }

  WEdge MakeEdge(int ei) {
    const ON_BrepEdge& e = b_.m_E[ei];
    WEdge we;
    we.on_edge = ei;
    we.sv = e.m_vi[0];
    we.ev = e.m_vi[1];
    const ON_Curve* c = e.EdgeCurveOf();
    if (!c) WRefuse("edge " + std::to_string(ei) + " has no 3D curve");
    const ON_3dPoint P0 = b_.m_V[we.sv].point, P1 = b_.m_V[we.ev].point;
    ON_Arc arc;
    if (c->IsLinear(kTol)) {
      ON_3dVector d = P1 - P0;
      const double len = d.Length();
      if (!(len > kTol)) WRefuse("edge " + std::to_string(ei) + " is a degenerate line");
      d.Unitize();
      we.curve.type = "straight-curve";
      we.curve.Hdr().Pos(P0).Vec(d).Ranges(2);
      we.t0 = 0.0;
      we.t1 = len;
    } else if (c->IsArc(nullptr, &arc, kTol) && arc.IsValid()) {
      NeedUnverified("edge " + std::to_string(ei), "ellipse-curve");
      const bool closed = c->IsClosed();
      const double a0 = arc.DomainRadians().Min(), a1 = arc.DomainRadians().Max();
      we.curve.type = "ellipse-curve";
      we.curve.Hdr().Pos(arc.Center()).Vec(arc.Plane().zaxis).Vec(arc.Plane().xaxis * arc.Radius()).Real(1).Ranges(2);
      we.t0 = a0;
      we.t1 = a1;
      if (!closed) {
        if (arc.StartPoint().DistanceTo(P0) < kTol * 10.0) we.reversed = false;
        else if (arc.EndPoint().DistanceTo(P0) < kTol * 10.0) we.reversed = true;
        else WRefuse("edge " + std::to_string(ei) + ": arc end points do not match its vertices");
      } else if (arc.StartPoint().DistanceTo(P0) > kTol * 10.0) {
        WRefuse("edge " + std::to_string(ei) + ": closed circle does not start at its vertex");
      }
    } else {
      WRefuse("edge " + std::to_string(ei) + " is neither a straight line nor a circular arc (other curves are not supported in this round)");
    }
    return we;
  }

  void Emit(std::vector<WRec>& recs) {
    // index allocation
    const int body_i = static_cast<int>(recs.size());
    int next = body_i;
    const int body_idx = next++;
    std::vector<int> lump_idx(shells_.size()), shell_idx(shells_.size());
    for (size_t s = 0; s < shells_.size(); ++s) lump_idx[s] = next++;
    for (size_t s = 0; s < shells_.size(); ++s) shell_idx[s] = next++;
    std::vector<int> face_idx(faces_.size()), loop_idx(loops_.size()), ce_idx(coedges_.size()), edge_idx(edges_.size()),
        vert_idx(vertices_.size()), pt_idx(vertices_.size()), surf_idx(faces_.size()), curve_idx(edges_.size());
    for (auto& x : face_idx) x = next++;
    for (auto& x : loop_idx) x = next++;
    for (auto& x : ce_idx) x = next++;
    for (auto& x : edge_idx) x = next++;
    for (auto& x : vert_idx) x = next++;
    for (auto& x : pt_idx) x = next++;
    for (auto& x : surf_idx) x = next++;
    for (auto& x : curve_idx) x = next++;

    // Starts a record (type + the header triple) and returns it for chaining.
    auto rec = [&](const char* type) -> WRec& {
      recs.emplace_back();
      recs.back().type = type;
      recs.back().Hdr();
      return recs.back();
    };
    rec("body").Ptr(-1).Ptr(lump_idx.front()).Ptr(-1).Ptr(-1);
    for (size_t s = 0; s < shells_.size(); ++s) {
      rec("lump").Ptr(s + 1 < shells_.size() ? lump_idx[s + 1] : -1).Ptr(shell_idx[s]).Ptr(body_idx);
    }
    for (size_t s = 0; s < shells_.size(); ++s) {
      rec("shell").Ptr(-1).Ptr(-1).Ptr(face_idx[static_cast<size_t>(shells_[s].front())]).Ptr(-1).Ptr(lump_idx[s]);
    }
    // faces: chain faces within a shell
    std::vector<int> next_face(faces_.size(), -1), shell_of_face(faces_.size(), 0);
    for (size_t s = 0; s < shells_.size(); ++s)
      for (size_t q = 0; q < shells_[s].size(); ++q) {
        shell_of_face[static_cast<size_t>(shells_[s][q])] = static_cast<int>(s);
        if (q + 1 < shells_[s].size()) next_face[static_cast<size_t>(shells_[s][q])] = face_idx[static_cast<size_t>(shells_[s][q + 1])];
      }
    std::vector<int> first_loop(faces_.size(), -1);
    std::vector<int> next_loop(loops_.size(), -1);
    for (size_t l = 0; l < loops_.size(); ++l) {
      const size_t f = static_cast<size_t>(loops_[l].face);
      if (first_loop[f] < 0) first_loop[f] = loop_idx[l];
      else {
        // append to the face's chain
        for (size_t m = 0; m < l; ++m)
          if (loops_[m].face == loops_[l].face && next_loop[m] < 0) {
            next_loop[m] = loop_idx[l];
            break;
          }
      }
    }
    for (size_t f = 0; f < faces_.size(); ++f) {
      WRec& r = rec("face").Ptr(next_face[f]).Ptr(first_loop[f]).Ptr(shell_idx[static_cast<size_t>(shell_of_face[f])]).Ptr(-1).Ptr(surf_idx[f]);
      r.Id(faces_[f].reversed ? "reversed" : "forward").Id(solid_ ? "single" : "double");
      if (!solid_) r.Id("out");
    }
    for (size_t l = 0; l < loops_.size(); ++l) {
      rec("loop").Ptr(next_loop[l]).Ptr(ce_idx[static_cast<size_t>(loops_[l].coedges.front())]).Ptr(face_idx[static_cast<size_t>(loops_[l].face)]);
    }
    for (size_t c = 0; c < coedges_.size(); ++c) {
      const WCoedge& ce = coedges_[c];
      rec("coedge")
          .Ptr(ce_idx[static_cast<size_t>(ce.next)])
          .Ptr(ce_idx[static_cast<size_t>(ce.prev)])
          .Ptr(ce.partner >= 0 ? ce_idx[static_cast<size_t>(ce.partner)] : -1)
          .Ptr(edge_idx[static_cast<size_t>(ce.edge)])
          .Id(ce.rev ? "reversed" : "forward")
          .Ptr(loop_idx[static_cast<size_t>(ce.loop)])
          .Ptr(-1);
    }
    std::map<int, int> edge_of_vertex;
    for (size_t e = 0; e < edges_.size(); ++e) {
      const WEdge& we = edges_[e];
      edge_of_vertex.emplace(we.sv, edge_idx[e]);
      edge_of_vertex.emplace(we.ev, edge_idx[e]);
      rec("edge")
          .Ptr(vert_idx[static_cast<size_t>(vertex_slot_[we.sv])])
          .Real(we.t0)
          .Ptr(vert_idx[static_cast<size_t>(vertex_slot_[we.ev])])
          .Real(we.t1)
          .Ptr(ce_idx[static_cast<size_t>(we.first_coedge)])
          .Ptr(curve_idx[e])
          .Id(we.reversed ? "reversed" : "forward")
          .Str("unknown");
    }
    for (size_t v = 0; v < vertices_.size(); ++v) {
      rec("vertex").Ptr(edge_of_vertex[vertices_[v]]).Ptr(pt_idx[v]);
    }
    for (size_t v = 0; v < vertices_.size(); ++v) rec("point").Pos(b_.m_V[vertices_[v]].point);
    for (size_t f = 0; f < faces_.size(); ++f) recs.push_back(faces_[f].surf);
    for (size_t e = 0; e < edges_.size(); ++e) recs.push_back(edges_[e].curve);
  }

  const ON_Brep& b_;
  std::vector<std::string>& warnings_;
  bool allow_ = false;
  std::vector<WFace> faces_;
  std::vector<WLoop> loops_;
  std::vector<WCoedge> coedges_;
  std::vector<WEdge> edges_;
  std::vector<int> vertices_;
  std::map<int, int> vertex_slot_;
  std::vector<std::vector<int>> shells_;
  bool solid_ = false;
  bool any_loopless_ = false;
};

}  // namespace

namespace {

// Collects every body's records. Returns false (with the reason in `res.error`) on refusal.
bool CollectRecords(const std::vector<Brep>& bodies, const AcisSatOptions& options, AcisSatWriteResult& res, std::vector<WRec>& recs) {
  if (bodies.empty()) {
    res.error = "no bodies to write";
    return false;
  }
  for (size_t b = 0; b < bodies.size(); ++b) {
    try {
      BodyWriter w(bodies[b].raw(), res.warnings, options.allow_unverified_layouts);
      w.Write(recs);
    } catch (const SatError& e) {
      res.error = "body " + std::to_string(b) + " cannot be written: " + e.what();
      return false;
    } catch (const std::exception& e) {
      res.error = "body " + std::to_string(b) + " cannot be written (internal error): " + e.what();
      return false;
    }
  }
  return true;
}

std::string TokText(const WTok& t) {
  switch (t.k) {
    case WTok::Ptr: return "$" + std::to_string(t.i);
    case WTok::Int: return std::to_string(t.i);
    case WTok::Real: return Num(t.d[0]);
    case WTok::Ident: return t.s;
    case WTok::Str: return "@" + std::to_string(t.s.size()) + " " + t.s;
    case WTok::Pos:
    case WTok::Vec: return Num(t.d[0]) + " " + Num(t.d[1]) + " " + Num(t.d[2]);
  }
  return std::string();
}

// The two-valued words and their bool (see "SAB wire format": UNVERIFIED choice).
// "F" is true and is followed by its double in the stream, exactly as in text.
bool SabBoolOf(const std::string& word) {
  static const char* const kFalse[] = {"forward", "single", "in", "forward_v", "I"};
  static const char* const kTrue[] = {"reversed", "double", "out", "reverse_v", "F"};
  for (const char* w : kFalse)
    if (word == w) return false;
  for (const char* w : kTrue)
    if (word == w) return true;
  throw SatError("internal: keyword '" + word + "' has no SAB bool encoding");
}

void PutU32(std::string& o, uint32_t v) {
  for (int i = 0; i < 4; ++i) o.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void PutF64(std::string& o, double v) {
  if (!std::isfinite(v) || v == 0.0) v = 0.0;  // same rule as the text writer's Num(): non-finite and -0 both become +0
  uint64_t u;
  std::memcpy(&u, &v, 8);
  for (int i = 0; i < 8; ++i) o.push_back(static_cast<char>((u >> (8 * i)) & 0xFF));
}
void PutStr8(std::string& o, const std::string& s) {
  if (s.size() > 255) throw SatError("string longer than 255 bytes cannot be written with the 8-bit length tag");
  o.push_back(static_cast<char>(sabtag::kStr8));
  o.push_back(static_cast<char>(s.size()));
  o += s;
}

std::string EncodeSab(const std::vector<WRec>& recs, size_t bodies) {
  std::string o;
  o.append(kSabMagic, kSabMagicLen);
  PutU32(o, 700u);
  PutU32(o, static_cast<uint32_t>(recs.size()));
  PutU32(o, static_cast<uint32_t>(bodies));
  PutU32(o, 0u);
  PutStr8(o, "Dino8 kernel");
  PutStr8(o, "ACIS 7.0 NT");
  PutStr8(o, "Mon Jan 01 00:00:00 2024");
  for (double v : {1.0, 1e-06, 1e-10}) {
    o.push_back(static_cast<char>(sabtag::kDouble));
    PutF64(o, v);
  }
  auto name = [&](const std::string& n) {
    if (n.empty() || n.size() > 255) throw SatError("entity name length out of range");
    o.push_back(static_cast<char>(sabtag::kName));
    o.push_back(static_cast<char>(n.size()));
    o += n;
  };
  for (const WRec& r : recs) {
    name(r.type);
    for (const WTok& t : r.f) {
      switch (t.k) {
        case WTok::Ptr:
          o.push_back(static_cast<char>(sabtag::kPtr));
          PutU32(o, static_cast<uint32_t>(static_cast<int32_t>(t.i)));
          break;
        case WTok::Int:
          o.push_back(static_cast<char>(sabtag::kInt));
          PutU32(o, static_cast<uint32_t>(static_cast<int32_t>(t.i)));
          break;
        case WTok::Real:
          o.push_back(static_cast<char>(sabtag::kDouble));
          PutF64(o, t.d[0]);
          break;
        case WTok::Ident:
          o.push_back(static_cast<char>(SabBoolOf(t.s) ? sabtag::kTrue : sabtag::kFalse));
          break;
        case WTok::Str:
          PutStr8(o, t.s);
          break;
        case WTok::Pos:
        case WTok::Vec:
          o.push_back(static_cast<char>(t.k == WTok::Pos ? sabtag::kPos : sabtag::kVec));
          for (double v : t.d) PutF64(o, v);
          break;
      }
    }
    o.push_back(static_cast<char>(sabtag::kTerm));
  }
  name(kSabEndName);
  o.push_back(static_cast<char>(sabtag::kTerm));
  return o;
}

AcisSatWriteResult FlushToFile(const std::string& path, const std::string& bytes, AcisSatWriteResult res) {
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    res.ok = false;
    res.error = "cannot open '" + path + "' for writing";
    return res;
  }
  f << bytes;
  f.flush();
  if (!f) {
    res.ok = false;
    res.error = "write to '" + path + "' failed";
  }
  return res;
}

}  // namespace

AcisSatWriteResult WriteAcisSat(std::ostream& out, const std::vector<Brep>& bodies, const AcisSatOptions& options) {
  AcisSatWriteResult res;
  std::vector<WRec> recs;
  if (!CollectRecords(bodies, options, res, recs)) return res;
  out << "700 0 " << bodies.size() << " 0\n";
  out << "@12 Dino8 kernel @11 ACIS 7.0 NT @24 Mon Jan 01 00:00:00 2024\n";
  out << "1 1e-06 1e-10\n";
  for (size_t i = 0; i < recs.size(); ++i) {
    out << "-" << i << " " << recs[i].type;
    for (const WTok& t : recs[i].f) out << " " << TokText(t);
    out << " #\n";
  }
  out << "End-of-ACIS-data\n";
  if (!out) {
    res.error = "write failed";
    return res;
  }
  res.ok = true;
  return res;
}

AcisSatWriteResult WriteAcisSat(const std::string& path, const std::vector<Brep>& bodies, const AcisSatOptions& options) {
  std::ostringstream os;
  AcisSatWriteResult res = WriteAcisSat(os, bodies, options);
  if (!res.ok) return res;
  return FlushToFile(path, os.str(), res);
}

AcisSatWriteResult WriteAcisSab(std::ostream& out, const std::vector<Brep>& bodies, const AcisSatOptions& options) {
  AcisSatWriteResult res;
  std::vector<WRec> recs;
  if (!CollectRecords(bodies, options, res, recs)) return res;
  std::string bytes;
  try {
    bytes = EncodeSab(recs, bodies.size());
  } catch (const std::exception& e) {
    res.error = std::string("cannot encode the SAB file: ") + e.what();
    return res;
  }
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    res.error = "write failed";
    return res;
  }
  res.ok = true;
  return res;
}

AcisSatWriteResult WriteAcisSab(const std::string& path, const std::vector<Brep>& bodies, const AcisSatOptions& options) {
  std::ostringstream os;
  AcisSatWriteResult res = WriteAcisSab(os, bodies, options);
  if (!res.ok) return res;
  return FlushToFile(path, os.str(), res);
}

}  // namespace dino8::kernel
