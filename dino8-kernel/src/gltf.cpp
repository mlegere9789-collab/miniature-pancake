#include "dino8/kernel/gltf.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>

namespace dino8::kernel {

namespace {

constexpr size_t kMaxFileBytes = size_t{1} << 30;
constexpr size_t kMaxElements = 100'000'000;  // vertices + triangles over the whole file, and again over everything the scene instantiates
constexpr int kMaxJsonDepth = 64;
constexpr int kMaxNodeDepth = 64;

// ---------------------------------------------------------------------------------------------------- JSON

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
  bool b = false;
  double n = 0.0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;

  const Json* Get(const char* key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : o) {
      if (kv.first == key) return &kv.second;
    }
    return nullptr;
  }
  bool IsNum() const { return type == Type::Number; }
  bool IsStr() const { return type == Type::String; }
  bool IsArr() const { return type == Type::Array; }
  bool IsObj() const { return type == Type::Object; }
};

class JsonParser {
 public:
  explicit JsonParser(const std::string& t) : t_(t) {}

  bool Parse(Json& out) {
    if (!Value(out, 0)) return false;
    Ws();
    return pos_ == t_.size();
  }

 private:
  const std::string& t_;
  size_t pos_ = 0;

  void Ws() {
    while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\r' || t_[pos_] == '\n')) ++pos_;
  }

  bool Literal(const char* word) {
    const size_t n = std::strlen(word);
    if (t_.compare(pos_, n, word) != 0) return false;
    pos_ += n;
    return true;
  }

  static void AppendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) s.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { s.push_back(static_cast<char>(0xC0 | (cp >> 6))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { s.push_back(static_cast<char>(0xE0 | (cp >> 12))); s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else { s.push_back(static_cast<char>(0xF0 | (cp >> 18))); s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
  }

  bool Hex4(unsigned& out) {
    if (pos_ + 4 > t_.size()) return false;
    unsigned v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = t_[pos_ + static_cast<size_t>(i)];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
      else return false;
    }
    pos_ += 4;
    out = v;
    return true;
  }

  bool String(std::string& out) {
    if (pos_ >= t_.size() || t_[pos_] != '"') return false;
    ++pos_;
    out.clear();
    while (pos_ < t_.size()) {
      const char c = t_[pos_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return false;
      if (c != '\\') { out.push_back(c); continue; }
      if (pos_ >= t_.size()) return false;
      const char e = t_[pos_++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          unsigned cp = 0;
          if (!Hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {  // a high surrogate: the low one must follow as its own \uXXXX
            unsigned lo = 0;
            if (t_.compare(pos_, 2, "\\u") != 0) return false;
            pos_ += 2;
            if (!Hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return false;
          }
          AppendUtf8(out, cp);
          break;
        }
        default: return false;
      }
    }
    return false;
  }

  // -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?  The text is checked against JSON's grammar here and converted by strtod (std::from_chars
  // for double is not in every standard library this is built with). A value that overflows a double is refused.
  bool Number(double& out) {
    const size_t begin = pos_;
    const auto digit = [&]() { return pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9'; };
    if (pos_ < t_.size() && t_[pos_] == '-') ++pos_;
    if (!digit()) return false;
    if (t_[pos_] == '0') {
      ++pos_;
    } else {
      while (digit()) ++pos_;
    }
    if (pos_ < t_.size() && t_[pos_] == '.') {
      ++pos_;
      if (!digit()) return false;
      while (digit()) ++pos_;
    }
    if (pos_ < t_.size() && (t_[pos_] == 'e' || t_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < t_.size() && (t_[pos_] == '+' || t_[pos_] == '-')) ++pos_;
      if (!digit()) return false;
      while (digit()) ++pos_;
    }
    const std::string text = t_.substr(begin, pos_ - begin);
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(out);
  }

  bool Value(Json& out, int depth) {
    if (depth > kMaxJsonDepth) return false;
    Ws();
    if (pos_ >= t_.size()) return false;
    const char c = t_[pos_];
    if (c == '{') {
      ++pos_;
      out.type = Json::Type::Object;
      Ws();
      if (pos_ < t_.size() && t_[pos_] == '}') { ++pos_; return true; }
      while (true) {
        Ws();
        std::string key;
        if (!String(key)) return false;
        Ws();
        if (pos_ >= t_.size() || t_[pos_] != ':') return false;
        ++pos_;
        Json v;
        if (!Value(v, depth + 1)) return false;
        out.o.emplace_back(std::move(key), std::move(v));
        Ws();
        if (pos_ >= t_.size()) return false;
        if (t_[pos_] == ',') { ++pos_; continue; }
        if (t_[pos_] == '}') { ++pos_; return true; }
        return false;
      }
    }
    if (c == '[') {
      ++pos_;
      out.type = Json::Type::Array;
      Ws();
      if (pos_ < t_.size() && t_[pos_] == ']') { ++pos_; return true; }
      while (true) {
        Json v;
        if (!Value(v, depth + 1)) return false;
        out.a.push_back(std::move(v));
        Ws();
        if (pos_ >= t_.size()) return false;
        if (t_[pos_] == ',') { ++pos_; continue; }
        if (t_[pos_] == ']') { ++pos_; return true; }
        return false;
      }
    }
    if (c == '"') { out.type = Json::Type::String; return String(out.s); }
    if (c == 't') { out.type = Json::Type::Bool; out.b = true; return Literal("true"); }
    if (c == 'f') { out.type = Json::Type::Bool; out.b = false; return Literal("false"); }
    if (c == 'n') { out.type = Json::Type::Null; return Literal("null"); }
    out.type = Json::Type::Number;
    return Number(out.n);
  }
};

// ---------------------------------------------------------------------------------------------------- base64

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const std::string& in) {
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]);
    out.push_back(kB64[(v >> 18) & 63]); out.push_back(kB64[(v >> 12) & 63]); out.push_back(kB64[(v >> 6) & 63]); out.push_back(kB64[v & 63]);
  }
  if (i + 1 == in.size()) {
    const unsigned v = static_cast<unsigned char>(in[i]) << 16;
    out.push_back(kB64[(v >> 18) & 63]); out.push_back(kB64[(v >> 12) & 63]); out += "==";
  } else if (i + 2 == in.size()) {
    const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
    out.push_back(kB64[(v >> 18) & 63]); out.push_back(kB64[(v >> 12) & 63]); out.push_back(kB64[(v >> 6) & 63]); out.push_back('=');
  }
  return out;
}

bool Base64Decode(const std::string& in, std::string& out) {
  out.clear();
  unsigned acc = 0;
  int bits = 0, pad = 0;
  for (char c : in) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+' || c == '-') v = 62;
    else if (c == '/' || c == '_') v = 63;
    else if (c == '=') { ++pad; continue; }
    else return false;
    if (pad) return false;  // data after padding
    acc = (acc << 6) | static_cast<unsigned>(v);
    bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((acc >> bits) & 0xFF)); }
  }
  return pad <= 2;
}

// ---------------------------------------------------------------------------------------------------- model

struct Affine {  // column-vector convention, 4x4 column-major like glTF's `matrix`
  double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

Affine Mul(const Affine& a, const Affine& b) {  // a * b: b first
  Affine c;
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      double s = 0;
      for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[col * 4 + k];
      c.m[col * 4 + row] = s;
    }
  }
  return c;
}

// Normals transform by the inverse transpose of the upper-left 3x3 (A). With A's rows a, b, c: A^-T has the rows b x c, c x a, a x b, all
// divided by det A = a . (b x c). The rows are kept undivided (the caller normalizes) with the determinant's sign carried separately.
struct NormalMatrix {
  double r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  double det = 1.0;
};

NormalMatrix NormalMatrixOf(const Affine& x) {
  const double* m = x.m;  // m[col * 4 + row]
  const double a[3] = {m[0], m[4], m[8]}, b[3] = {m[1], m[5], m[9]}, c[3] = {m[2], m[6], m[10]};
  NormalMatrix n;
  const auto cross = [](const double* u, const double* v, double* out) {
    out[0] = u[1] * v[2] - u[2] * v[1];
    out[1] = u[2] * v[0] - u[0] * v[2];
    out[2] = u[0] * v[1] - u[1] * v[0];
  };
  cross(b, c, n.r[0]);
  cross(c, a, n.r[1]);
  cross(a, b, n.r[2]);
  n.det = a[0] * n.r[0][0] + a[1] * n.r[0][1] + a[2] * n.r[0][2];
  return n;
}

bool NumArray(const Json* j, size_t n, double* out) {
  if (!j || !j->IsArr() || j->a.size() != n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (!j->a[i].IsNum()) return false;
    out[i] = j->a[i].n;
  }
  return true;
}

bool IntOf(const Json* j, int64_t& out) {
  if (!j || !j->IsNum() || j->n != std::floor(j->n) || std::fabs(j->n) > 9e15) return false;
  out = static_cast<int64_t>(j->n);
  return true;
}

float Clamp01(double v) { return static_cast<float>(std::min(1.0, std::max(0.0, v))); }

// One material object of the file. A term of the wrong type or length keeps the specification's default.
GltfMaterial ParseMaterial(const Json& m) {
  GltfMaterial out;
  if (const Json* nm = m.Get("name")) { if (nm->IsStr()) out.name = nm->s; }
  if (const Json* pbr = m.Get("pbrMetallicRoughness")) {
    double c[4];
    if (NumArray(pbr->Get("baseColorFactor"), 4, c)) out.base_color = {Clamp01(c[0]), Clamp01(c[1]), Clamp01(c[2]), Clamp01(c[3])};
    if (const Json* j = pbr->Get("metallicFactor")) { if (j->IsNum()) out.metallic = Clamp01(j->n); }
    if (const Json* j = pbr->Get("roughnessFactor")) { if (j->IsNum()) out.roughness = Clamp01(j->n); }
  }
  double e[3];
  if (NumArray(m.Get("emissiveFactor"), 3, e)) out.emissive = {Clamp01(e[0]), Clamp01(e[1]), Clamp01(e[2])};
  if (const Json* j = m.Get("alphaMode")) {
    if (j->IsStr() && j->s == "MASK") out.alpha_mode = GltfMaterial::AlphaMode::Mask;
    else if (j->IsStr() && j->s == "BLEND") out.alpha_mode = GltfMaterial::AlphaMode::Blend;
  }
  if (const Json* j = m.Get("alphaCutoff")) { if (j->IsNum() && j->n >= 0.0) out.alpha_cutoff = static_cast<float>(j->n); }
  if (const Json* j = m.Get("doubleSided")) { if (j->type == Json::Type::Bool) out.double_sided = j->b; }
  return out;
}

struct Primitive {  // one triangle primitive, in its mesh's own space
  std::vector<float> verts;    // x y z ...
  std::vector<float> normals;  // x y z ..., empty or one per vertex
  std::vector<int32_t> tris;   // a b c ...
  int64_t material = -1;
};

struct Geometry {
  std::string name;
  std::vector<Primitive> prims;
};

struct Reader {
  Json root;
  std::string dir;
  std::string bin;  // the GLB BIN chunk
  bool have_bin = false;
  std::map<int64_t, std::string> buffers;  // loaded lazily
  size_t elements = 0;

  const Json* Arr(const char* key) const {
    const Json* a = root.Get(key);
    return a && a->IsArr() ? a : nullptr;
  }

  static bool SafeRelative(const std::string& uri) {
    if (uri.empty() || uri[0] == '/' || uri[0] == '\\' || uri.find(':') != std::string::npos) return false;
    std::string seg;
    for (size_t i = 0; i <= uri.size(); ++i) {
      if (i == uri.size() || uri[i] == '/' || uri[i] == '\\') {
        if (seg == "..") return false;
        seg.clear();
      } else {
        seg.push_back(uri[i]);
      }
    }
    return true;
  }

  static std::string PercentDecode(const std::string& s) {
    const auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
        o.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
        i += 2;
      } else {
        o.push_back(s[i]);
      }
    }
    return o;
  }

  const std::string* Buffer(int64_t index) {
    const auto it = buffers.find(index);
    if (it != buffers.end()) return &it->second;
    const Json* bs = Arr("buffers");
    if (!bs || index < 0 || static_cast<size_t>(index) >= bs->a.size() || !bs->a[static_cast<size_t>(index)].IsObj()) return nullptr;
    const Json& b = bs->a[static_cast<size_t>(index)];
    int64_t len = 0;
    if (!IntOf(b.Get("byteLength"), len) || len < 0) return nullptr;
    std::string data;
    const Json* uri = b.Get("uri");
    if (!uri) {
      if (!(index == 0 && have_bin)) return nullptr;
      data = bin;
    } else if (!uri->IsStr()) {
      return nullptr;
    } else if (uri->s.compare(0, 5, "data:") == 0) {
      const size_t comma = uri->s.find(',');
      if (comma == std::string::npos || uri->s.substr(0, comma).find(";base64") == std::string::npos) return nullptr;
      if (!Base64Decode(uri->s.substr(comma + 1), data)) return nullptr;
    } else {
      const std::string rel = PercentDecode(uri->s);
      if (!SafeRelative(rel)) return nullptr;
      std::ifstream in(dir + rel, std::ios::binary);
      if (!in) return nullptr;
      data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (static_cast<int64_t>(data.size()) < len) return nullptr;
    data.resize(static_cast<size_t>(len));
    return &(buffers[index] = std::move(data));
  }

  // Reads accessor `index` into `out` as doubles, `components` per element (1 for SCALAR, 3 for VEC3); the accessor's type must match.
  // `float_or_normalized`: the values must be real numbers (float, or integers marked `normalized`), as a unit normal has to be.
  bool Accessor(int64_t index, int components, std::vector<double>& out, bool float_or_normalized = false) {
    const Json* as = Arr("accessors");
    if (!as || index < 0 || static_cast<size_t>(index) >= as->a.size() || !as->a[static_cast<size_t>(index)].IsObj()) return false;
    const Json& a = as->a[static_cast<size_t>(index)];
    if (a.Get("sparse")) return false;
    const Json* type = a.Get("type");
    const char* want = components == 1 ? "SCALAR" : "VEC3";
    if (!type || !type->IsStr() || type->s != want) return false;
    int64_t comp_type = 0, count = 0, view_index = -1, acc_off = 0;
    if (!IntOf(a.Get("componentType"), comp_type) || !IntOf(a.Get("count"), count) || count < 0) return false;
    if (const Json* o = a.Get("byteOffset")) { if (!IntOf(o, acc_off) || acc_off < 0) return false; }
    if (!IntOf(a.Get("bufferView"), view_index)) return false;
    const bool normalized = a.Get("normalized") && a.Get("normalized")->type == Json::Type::Bool && a.Get("normalized")->b;
    size_t csize = 0;
    switch (comp_type) {
      case 5120: case 5121: csize = 1; break;
      case 5122: case 5123: csize = 2; break;
      case 5125: case 5126: csize = 4; break;
      default: return false;
    }
    if (components == 1 && comp_type == 5126) return false;  // indices are integers
    if (float_or_normalized && comp_type != 5126 && !normalized) return false;
    const Json* vs = Arr("bufferViews");
    if (!vs || view_index < 0 || static_cast<size_t>(view_index) >= vs->a.size() || !vs->a[static_cast<size_t>(view_index)].IsObj()) return false;
    const Json& v = vs->a[static_cast<size_t>(view_index)];
    int64_t buf_index = 0, view_off = 0, view_len = 0, stride = 0;
    if (!IntOf(v.Get("buffer"), buf_index) || !IntOf(v.Get("byteLength"), view_len) || view_len < 0) return false;
    if (const Json* o = v.Get("byteOffset")) { if (!IntOf(o, view_off) || view_off < 0) return false; }
    const size_t elem_bytes = csize * static_cast<size_t>(components);
    if (const Json* s = v.Get("byteStride")) { if (!IntOf(s, stride) || stride < static_cast<int64_t>(elem_bytes) || stride > 252) return false; }
    const std::string* buf = Buffer(buf_index);
    if (!buf || view_off + view_len > static_cast<int64_t>(buf->size())) return false;
    const size_t step = stride > 0 ? static_cast<size_t>(stride) : elem_bytes;
    if (count > 0) {
      const uint64_t last_end = static_cast<uint64_t>(acc_off) + step * static_cast<uint64_t>(count - 1) + elem_bytes;
      if (last_end > static_cast<uint64_t>(view_len)) return false;
    }
    if (static_cast<uint64_t>(count) > kMaxElements * 3) return false;
    out.resize(static_cast<size_t>(count) * static_cast<size_t>(components));
    const unsigned char* base = reinterpret_cast<const unsigned char*>(buf->data()) + view_off + acc_off;
    for (int64_t i = 0; i < count; ++i) {
      for (int c = 0; c < components; ++c) {
        const unsigned char* p = base + static_cast<size_t>(i) * step + static_cast<size_t>(c) * csize;
        double val = 0;
        switch (comp_type) {
          case 5120: { int8_t x = static_cast<int8_t>(*p); val = normalized ? std::max(x / 127.0, -1.0) : x; break; }
          case 5121: { val = normalized ? *p / 255.0 : *p; break; }
          case 5122: { int16_t x; std::memcpy(&x, p, 2); val = normalized ? std::max(x / 32767.0, -1.0) : x; break; }
          case 5123: { uint16_t x; std::memcpy(&x, p, 2); val = normalized ? x / 65535.0 : x; break; }
          case 5125: { uint32_t x; std::memcpy(&x, p, 4); val = normalized ? x / 4294967295.0 : static_cast<double>(x); break; }
          case 5126: { float x; std::memcpy(&x, p, 4); val = x; if (!std::isfinite(x)) return false; break; }
        }
        out[static_cast<size_t>(i) * static_cast<size_t>(components) + static_cast<size_t>(c)] = val;
      }
    }
    return true;
  }

  // Meshes -> geometry, one Primitive per triangle primitive; false on a malformed mesh / primitive.
  bool BuildMeshes(std::vector<Geometry>& out) {
    const Json* ms = Arr("meshes");
    if (!ms) return true;
    const Json* mats = Arr("materials");
    for (const Json& m : ms->a) {
      Geometry g;
      if (!m.IsObj()) return false;
      if (const Json* nm = m.Get("name")) { if (nm->IsStr()) g.name = nm->s; }
      const Json* prims = m.Get("primitives");
      if (!prims || !prims->IsArr()) return false;
      for (const Json& p : prims->a) {
        if (!p.IsObj()) return false;
        int64_t mode = 4;
        if (const Json* mj = p.Get("mode")) { if (!IntOf(mj, mode)) return false; }
        if (mode != 4 && mode != 5 && mode != 6) continue;  // points / lines / loops are not triangles
        const Json* attrs = p.Get("attributes");
        int64_t pos_acc = 0;
        if (!attrs || !attrs->IsObj() || !IntOf(attrs->Get("POSITION"), pos_acc)) return false;
        std::vector<double> pos;
        if (!Accessor(pos_acc, 3, pos)) return false;
        const size_t nv = pos.size() / 3;
        std::vector<double> nrm;
        if (const Json* nj = attrs->Get("NORMAL")) {
          int64_t na = 0;
          if (!IntOf(nj, na) || !Accessor(na, 3, nrm, /*float_or_normalized=*/true) || nrm.size() != pos.size()) return false;
        }
        std::vector<int64_t> idx;
        if (const Json* ij = p.Get("indices")) {
          int64_t ia = 0;
          if (!IntOf(ij, ia)) return false;
          std::vector<double> raw;
          if (!Accessor(ia, 1, raw)) return false;
          idx.reserve(raw.size());
          for (double d : raw) idx.push_back(static_cast<int64_t>(d));
        } else {
          for (size_t i = 0; i < nv; ++i) idx.push_back(static_cast<int64_t>(i));
        }
        for (int64_t i : idx) { if (i < 0 || static_cast<size_t>(i) >= nv) return false; }
        Primitive prim;
        std::vector<int32_t>& tri = prim.tris;
        const auto push = [&](int64_t a, int64_t b, int64_t c) {
          tri.push_back(static_cast<int32_t>(a)); tri.push_back(static_cast<int32_t>(b)); tri.push_back(static_cast<int32_t>(c));
        };
        if (nv > static_cast<size_t>(INT32_MAX)) return false;
        if (mode == 4) {
          for (size_t i = 0; i + 2 < idx.size(); i += 3) push(idx[i], idx[i + 1], idx[i + 2]);
        } else if (mode == 5) {
          for (size_t i = 0; i + 2 < idx.size(); ++i) {
            if (i % 2 == 0) push(idx[i], idx[i + 1], idx[i + 2]);
            else push(idx[i + 1], idx[i], idx[i + 2]);
          }
        } else {
          for (size_t i = 1; i + 1 < idx.size(); ++i) push(idx[0], idx[i], idx[i + 1]);
        }
        elements += nv + tri.size() / 3;
        if (elements > kMaxElements) return false;
        prim.verts.reserve(pos.size());
        for (double d : pos) prim.verts.push_back(static_cast<float>(d));
        prim.normals.reserve(nrm.size());
        for (double d : nrm) prim.normals.push_back(static_cast<float>(d));
        if (const Json* mj = p.Get("material")) {
          int64_t mi = -1;
          if (!IntOf(mj, mi) || !mats || mi < 0 || static_cast<size_t>(mi) >= mats->a.size() || !mats->a[static_cast<size_t>(mi)].IsObj()) return false;
          prim.material = mi;
        }
        g.prims.push_back(std::move(prim));
      }
      out.push_back(std::move(g));
    }
    return true;
  }
};

bool LocalTransform(const Json& node, Affine& out) {
  double v[16];
  if (const Json* mj = node.Get("matrix")) {
    if (!NumArray(mj, 16, v)) return false;
    for (int i = 0; i < 16; ++i) out.m[i] = v[i];
    return true;
  }
  double t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
  if (const Json* j = node.Get("translation")) { if (!NumArray(j, 3, t)) return false; }
  if (const Json* j = node.Get("rotation")) { if (!NumArray(j, 4, r)) return false; }
  if (const Json* j = node.Get("scale")) { if (!NumArray(j, 3, s)) return false; }
  const double len = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
  if (!(len > 1e-9)) return false;
  const double x = r[0] / len, y = r[1] / len, z = r[2] / len, w = r[3] / len;
  // Rotation matrix from the unit quaternion, scaled by s (columns), translated by t.
  const double rot[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                            {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                            {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
  for (int col = 0; col < 3; ++col) {
    for (int row = 0; row < 3; ++row) out.m[col * 4 + row] = rot[row][col] * s[col];
    out.m[col * 4 + 3] = 0;
  }
  out.m[12] = t[0]; out.m[13] = t[1]; out.m[14] = t[2]; out.m[15] = 1;
  return true;
}

// A JSON string body. Control characters are escaped; a byte sequence that is not valid UTF-8 (JSON text has to be) becomes U+FFFD.
std::string JsonEscape(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
          if (c < 0x20 || c == 0x7F) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
          else o.push_back(static_cast<char>(c));
      }
      ++i;
      continue;
    }
    size_t n = 0;
    unsigned cp = 0;
    if (c >= 0xC2 && c <= 0xDF) { n = 1; cp = c & 0x1Fu; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 2; cp = c & 0x0Fu; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 0x07u; }
    bool ok = n > 0 && i + n < s.size() + 0 + 1 && i + n <= s.size() - 1 + 1;
    if (ok && i + n >= s.size() + 1) ok = false;
    for (size_t k = 1; ok && k <= n; ++k) {
      if (i + k >= s.size()) { ok = false; break; }
      const unsigned char t = static_cast<unsigned char>(s[i + k]);
      if ((t & 0xC0) != 0x80) { ok = false; break; }
      cp = (cp << 6) | (t & 0x3Fu);
    }
    // Overlong forms, surrogates and code points past U+10FFFF are not UTF-8 either.
    if (ok && ((n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)) ok = false;
    if (ok) {
      o.append(s, i, n + 1);
      i += n + 1;
    } else {
      o += "\xEF\xBF\xBD";
      ++i;
    }
  }
  return o;
}

// The shortest decimal text that reads back as exactly `v`, in JSON's number syntax (std::to_chars for float is not in every standard
// library this is built with, so the precision is searched with snprintf / strtof).
void AppendFloat(std::string& s, float v) {
  if (v == 0.0f) { s += "0"; return; }  // also -0
  char buf[64];
  for (int prec = 1; prec <= 9; ++prec) {
    std::snprintf(buf, sizeof(buf), "%.*g", prec, static_cast<double>(v));
    if (std::strtof(buf, nullptr) == v) break;
  }
  for (char* p = buf; *p; ++p) { if (*p == ',') *p = '.'; }  // a host program that changed the C locale's decimal point
  s += buf;
}

// RFC 3986 relative reference for a file name: everything but the unreserved characters is percent-encoded.
std::string PercentEncode(const std::string& name) {
  std::string o;
  for (unsigned char c : name) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
      o.push_back(static_cast<char>(c));
    } else {
      char b[8];
      std::snprintf(b, sizeof(b), "%%%02X", c);
      o += b;
    }
  }
  return o;
}

void PutU32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); }
void PutF32(std::string& s, float v) { uint32_t u; std::memcpy(&u, &v, 4); PutU32(s, u); }

bool Finite01(const GltfMaterial& m) {
  for (float c : m.base_color) { if (!std::isfinite(c)) return false; }
  for (float c : m.emissive) { if (!std::isfinite(c)) return false; }
  return std::isfinite(m.metallic) && std::isfinite(m.roughness) && std::isfinite(m.alpha_cutoff);
}

std::string MaterialJson(const GltfMaterial& m) {
  std::string j = "{ ";
  if (!m.name.empty()) j += "\"name\": \"" + JsonEscape(m.name) + "\", ";
  j += "\"pbrMetallicRoughness\": { \"baseColorFactor\": [";
  for (size_t k = 0; k < 4; ++k) { if (k) j += ", "; AppendFloat(j, Clamp01(m.base_color[k])); }
  j += "], \"metallicFactor\": ";
  AppendFloat(j, Clamp01(m.metallic));
  j += ", \"roughnessFactor\": ";
  AppendFloat(j, Clamp01(m.roughness));
  j += " }";
  if (m.emissive[0] > 0.0f || m.emissive[1] > 0.0f || m.emissive[2] > 0.0f) {
    j += ", \"emissiveFactor\": [";
    for (size_t k = 0; k < 3; ++k) { if (k) j += ", "; AppendFloat(j, Clamp01(m.emissive[k])); }
    j += "]";
  }
  if (m.alpha_mode == GltfMaterial::AlphaMode::Mask) {
    j += ", \"alphaMode\": \"MASK\", \"alphaCutoff\": ";
    AppendFloat(j, std::max(0.0f, m.alpha_cutoff));
  } else if (m.alpha_mode == GltfMaterial::AlphaMode::Blend) {
    j += ", \"alphaMode\": \"BLEND\"";
  }
  if (m.double_sided) j += ", \"doubleSided\": true";
  j += " }";
  return j;
}

}  // namespace

Result SaveGltfModel(const std::string& path, const GltfModel& model, const GltfWriteOptions& options) {
  if (model.parts.empty() || !std::isfinite(options.scale) || !(options.scale > 0.0)) return Result::Failed;
  std::string bin;
  std::string accessors, views, meshes, nodes, materials;
  std::vector<GltfMaterial> written_materials;
  int accessor_count = 0, view_count = 0;
  int part_index = 0;
  for (const GltfPart& part : model.parts) {
    const ON_Mesh& m = part.mesh.raw();
    const int nv = m.m_V.Count();
    if (nv <= 0 || m.m_F.Count() <= 0) return Result::Failed;
    // Every index is checked before anything walks the faces (the normal computation indexes the vertex list directly).
    for (int f = 0; f < m.m_F.Count(); ++f) {
      const ON_MeshFace& face = m.m_F[f];
      for (int k = 0; k < 4; ++k) { if (face.vi[k] < 0 || face.vi[k] >= nv) return Result::Failed; }
    }
    if (part.material && !Finite01(*part.material)) return Result::Failed;

    double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
    const size_t pos_offset = bin.size();
    for (int i = 0; i < nv; ++i) {
      const ON_3fPoint& p = m.m_V[i];
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return Result::Failed;
      double c[3] = {p.x * options.scale, p.y * options.scale, p.z * options.scale};
      if (options.z_up_to_y_up) { const double y = c[1], z = c[2]; c[1] = z; c[2] = -y; }
      float f[3] = {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])};
      for (int k = 0; k < 3; ++k) {
        if (!std::isfinite(f[k])) return Result::Failed;  // the scale pushed it past float range
        if (f[k] == 0.0f) f[k] = 0.0f;                    // -0 -> 0
        lo[k] = std::min(lo[k], static_cast<double>(f[k]));
        hi[k] = std::max(hi[k], static_cast<double>(f[k]));
        PutF32(bin, f[k]);
      }
    }
    const size_t nrm_offset = bin.size();
    if (options.normals != GltfNormals::None) {
      bool stored = options.normals == GltfNormals::Stored && m.m_N.Count() == nv;
      for (int i = 0; stored && i < nv; ++i) {
        const ON_3fVector& n = m.m_N[i];
        stored = std::isfinite(n.x) && std::isfinite(n.y) && std::isfinite(n.z) && (static_cast<double>(n.x) * n.x + static_cast<double>(n.y) * n.y + static_cast<double>(n.z) * n.z) > 1e-12;
      }
      std::vector<Vector3d> computed;
      if (!stored) computed = part.mesh.ComputeVertexNormals();
      for (int i = 0; i < nv; ++i) {
        double n[3];
        if (stored) { n[0] = m.m_N[i].x; n[1] = m.m_N[i].y; n[2] = m.m_N[i].z; }
        else { n[0] = computed[static_cast<size_t>(i)].x; n[1] = computed[static_cast<size_t>(i)].y; n[2] = computed[static_cast<size_t>(i)].z; }
        const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (std::isfinite(len) && len > 0.0) { n[0] /= len; n[1] /= len; n[2] /= len; }
        else { n[0] = 0; n[1] = 0; n[2] = 1; }  // nothing gives this vertex a normal: up (Z here, Y after the rotation below)
        if (options.z_up_to_y_up) { const double y = n[1], z = n[2]; n[1] = z; n[2] = -y; }
        else if (!(std::isfinite(len) && len > 0.0)) { n[1] = 1; n[2] = 0; }  // the file's own up when no rotation is asked for
        for (int k = 0; k < 3; ++k) {
          float f = static_cast<float>(n[k]);
          if (f == 0.0f) f = 0.0f;
          PutF32(bin, f);
        }
      }
    }
    const bool has_normals = bin.size() > nrm_offset;
    const size_t idx_offset = bin.size();
    int tri_count = 0;
    const auto tri = [&](int a, int b, int c) {
      PutU32(bin, static_cast<uint32_t>(a)); PutU32(bin, static_cast<uint32_t>(b)); PutU32(bin, static_cast<uint32_t>(c));
      ++tri_count;
    };
    for (int f = 0; f < m.m_F.Count(); ++f) {
      const ON_MeshFace& face = m.m_F[f];
      tri(face.vi[0], face.vi[1], face.vi[2]);
      if (face.IsQuad()) tri(face.vi[0], face.vi[2], face.vi[3]);
    }

    const std::string sep = part_index ? ", " : "";
    std::string minmax_lo = "[", minmax_hi = "[";
    for (int k = 0; k < 3; ++k) {
      if (k) { minmax_lo += ", "; minmax_hi += ", "; }
      AppendFloat(minmax_lo, static_cast<float>(lo[k]));
      AppendFloat(minmax_hi, static_cast<float>(hi[k]));
    }
    minmax_lo += "]"; minmax_hi += "]";
    const int pos_acc = accessor_count++;
    views += sep + "{ \"buffer\": 0, \"byteOffset\": " + std::to_string(pos_offset) + ", \"byteLength\": " + std::to_string(nrm_offset - pos_offset) + ", \"target\": 34962 }";
    accessors += sep + "{ \"bufferView\": " + std::to_string(view_count++) + ", \"componentType\": 5126, \"count\": " + std::to_string(nv) + ", \"type\": \"VEC3\", \"min\": " + minmax_lo + ", \"max\": " + minmax_hi + " }";
    std::string attributes = "\"POSITION\": " + std::to_string(pos_acc);
    if (has_normals) {
      const int nrm_acc = accessor_count++;
      views += ", { \"buffer\": 0, \"byteOffset\": " + std::to_string(nrm_offset) + ", \"byteLength\": " + std::to_string(idx_offset - nrm_offset) + ", \"target\": 34962 }";
      accessors += ", { \"bufferView\": " + std::to_string(view_count++) + ", \"componentType\": 5126, \"count\": " + std::to_string(nv) + ", \"type\": \"VEC3\" }";
      attributes += ", \"NORMAL\": " + std::to_string(nrm_acc);
    }
    const int idx_acc = accessor_count++;
    views += ", { \"buffer\": 0, \"byteOffset\": " + std::to_string(idx_offset) + ", \"byteLength\": " + std::to_string(bin.size() - idx_offset) + ", \"target\": 34963 }";
    accessors += ", { \"bufferView\": " + std::to_string(view_count++) + ", \"componentType\": 5125, \"count\": " + std::to_string(tri_count * 3) + ", \"type\": \"SCALAR\" }";

    std::string material;
    if (part.material) {
      size_t mi = 0;
      for (; mi < written_materials.size(); ++mi) { if (written_materials[mi] == *part.material) break; }
      if (mi == written_materials.size()) {
        written_materials.push_back(*part.material);
        materials += std::string(materials.empty() ? "" : ", ") + MaterialJson(*part.material);
      }
      material = ", \"material\": " + std::to_string(mi);
    }
    const std::string name = part.name.empty() ? std::string() : "\"name\": \"" + JsonEscape(part.name) + "\", ";
    meshes += sep + "{ " + name + "\"primitives\": [ { \"attributes\": { " + attributes + " }, \"indices\": " + std::to_string(idx_acc) + ", \"mode\": 4" + material + " } ] }";
    nodes += sep + "{ " + name + "\"mesh\": " + std::to_string(part_index) + " }";
    ++part_index;
    if (bin.size() >= 0xFFFFFFF0ull) return Result::Failed;  // a glTF buffer / GLB chunk length is 32 bits
  }
  std::string scene_nodes;
  for (int i = 0; i < part_index; ++i) scene_nodes += (i ? ", " : "") + std::to_string(i);

  const std::filesystem::path file_path(path);
  std::filesystem::path bin_path = file_path;
  bin_path.replace_extension(".bin");
  const bool external = !options.binary && options.external_bin;
  if (external && bin_path == file_path) return Result::Failed;  // the model itself is called *.bin: no second name to give the buffer

  std::string buffer_item = "{ ";
  if (external) buffer_item += "\"uri\": \"" + PercentEncode(bin_path.filename().string()) + "\", ";
  else if (!options.binary) buffer_item += "\"uri\": \"data:application/octet-stream;base64," + Base64Encode(bin) + "\", ";
  buffer_item += "\"byteLength\": " + std::to_string(bin.size()) + " }";
  std::string json = "{\n  \"asset\": { \"version\": \"2.0\", \"generator\": \"Dino 8\" },\n  \"scene\": 0,\n  \"scenes\": [ { \"nodes\": [" + scene_nodes + "] } ],\n  \"nodes\": [" + nodes + "],\n  \"meshes\": [" + meshes + "],\n";
  if (!materials.empty()) json += "  \"materials\": [" + materials + "],\n";
  json += "  \"accessors\": [" + accessors + "],\n  \"bufferViews\": [" + views + "],\n  \"buffers\": [" + buffer_item + "]\n}\n";

  std::string out;
  if (options.binary) {
    while (json.size() % 4 != 0) json.push_back(' ');
    std::string bin_chunk = bin;
    while (bin_chunk.size() % 4 != 0) bin_chunk.push_back('\0');
    if (12 + 8 + json.size() + 8 + bin_chunk.size() >= 0xFFFFFFFFull) return Result::Failed;
    PutU32(out, 0x46546C67u); PutU32(out, 2); PutU32(out, static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin_chunk.size()));
    PutU32(out, static_cast<uint32_t>(json.size())); PutU32(out, 0x4E4F534Au); out += json;
    PutU32(out, static_cast<uint32_t>(bin_chunk.size())); PutU32(out, 0x004E4942u); out += bin_chunk;
  } else {
    out = json;
  }
  if (external) {
    std::ofstream b(bin_path, std::ios::binary);
    if (!b) return Result::Failed;
    b.write(bin.data(), static_cast<std::streamsize>(bin.size()));
    b.close();
    if (!b.good()) return Result::Failed;
  }
  std::ofstream f(path, std::ios::binary);
  if (!f) return Result::Failed;
  f.write(out.data(), static_cast<std::streamsize>(out.size()));
  f.close();
  return f.good() ? Result::Ok : Result::Failed;
}

Result LoadGltfModel(const std::string& path, GltfModel& model, const GltfReadOptions& options) {
  if (!std::isfinite(options.scale) || !(options.scale > 0.0)) return Result::Failed;
  std::ifstream in(path, std::ios::binary);
  if (!in) return Result::Failed;
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (data.size() < 2 || data.size() > kMaxFileBytes) return Result::Failed;

  Reader rd;
  std::string json_text;
  if (data.compare(0, 4, "glTF") == 0) {
    if (data.size() < 20) return Result::Failed;
    auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, data.data() + at, 4); return v; };
    if (u32(4) != 2 || u32(8) > data.size() || u32(8) < 20) return Result::Failed;
    const size_t total = u32(8);
    bool have_json = false;
    size_t off = 12;
    while (off + 8 <= total) {
      const size_t len = u32(off), type = u32(off + 4);
      off += 8;
      if (off + len > total) return Result::Failed;
      if (type == 0x4E4F534Au && !have_json) { json_text = data.substr(off, len); have_json = true; }
      else if (type == 0x004E4942u && !rd.have_bin) { rd.bin = data.substr(off, len); rd.have_bin = true; }
      off += len;
    }
    if (!have_json) return Result::Failed;
  } else {
    size_t start = 0;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF && static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) start = 3;
    json_text = data.substr(start);
  }
  data.clear();
  data.shrink_to_fit();

  {
    JsonParser p(json_text);
    if (!p.Parse(rd.root) || !rd.root.IsObj()) return Result::Failed;
  }
  if (const Json* asset = rd.root.Get("asset")) {
    const Json* v = asset->Get("version");
    if (!v || !v->IsStr() || v->s.compare(0, 2, "2.") != 0) return Result::Failed;
  }
  if (const Json* req = rd.root.Get("extensionsRequired")) {
    if (!req->IsArr() || !req->a.empty()) return Result::Failed;
  }
  {
    const std::filesystem::path pp(path);
    rd.dir = pp.has_parent_path() ? pp.parent_path().string() + "/" : std::string();
  }

  std::vector<Geometry> geoms;
  if (!rd.BuildMeshes(geoms)) return Result::Failed;
  if (geoms.empty()) return Result::Failed;
  std::vector<GltfMaterial> materials;
  if (const Json* mats = rd.Arr("materials")) {
    for (const Json& m : mats->a) materials.push_back(m.IsObj() ? ParseMaterial(m) : GltfMaterial());
  }

  // Instances: every node with a mesh in the chosen scene, in world space. No nodes: each mesh once at the identity.
  struct Instance { size_t geometry; Affine xf; std::string node_name; };
  std::vector<Instance> instances;
  const Json* nodes = rd.Arr("nodes");
  if (!nodes || nodes->a.empty()) {
    for (size_t i = 0; i < geoms.size(); ++i) instances.push_back({i, Affine(), std::string()});
  } else {
    std::vector<int64_t> roots;
    const Json* scenes = rd.Arr("scenes");
    if (scenes && !scenes->a.empty()) {
      int64_t si = 0;
      if (const Json* sj = rd.root.Get("scene")) { if (!IntOf(sj, si)) return Result::Failed; }
      if (si < 0 || static_cast<size_t>(si) >= scenes->a.size() || !scenes->a[static_cast<size_t>(si)].IsObj()) return Result::Failed;
      const Json* sn = scenes->a[static_cast<size_t>(si)].Get("nodes");
      if (sn && sn->IsArr()) {
        for (const Json& j : sn->a) { int64_t n; if (!IntOf(&j, n)) return Result::Failed; roots.push_back(n); }
      }
    } else {
      std::set<int64_t> children;
      for (const Json& nd : nodes->a) {
        const Json* ch = nd.Get("children");
        if (ch && ch->IsArr()) for (const Json& j : ch->a) { int64_t n; if (IntOf(&j, n)) children.insert(n); }
      }
      for (size_t i = 0; i < nodes->a.size(); ++i) { if (!children.count(static_cast<int64_t>(i))) roots.push_back(static_cast<int64_t>(i)); }
    }
    bool bad = false;
    std::vector<int64_t> on_stack;
    std::function<void(int64_t, const Affine&, int)> visit = [&](int64_t ni, const Affine& parent, int depth) {
      if (bad) return;
      if (ni < 0 || static_cast<size_t>(ni) >= nodes->a.size() || !nodes->a[static_cast<size_t>(ni)].IsObj() || depth > kMaxNodeDepth ||
          std::find(on_stack.begin(), on_stack.end(), ni) != on_stack.end()) { bad = true; return; }
      const Json& nd = nodes->a[static_cast<size_t>(ni)];
      Affine local;
      if (!LocalTransform(nd, local)) { bad = true; return; }
      const Affine world = Mul(parent, local);
      if (const Json* mj = nd.Get("mesh")) {
        int64_t mi;
        if (!IntOf(mj, mi) || mi < 0 || static_cast<size_t>(mi) >= geoms.size()) { bad = true; return; }
        const Json* nm = nd.Get("name");
        instances.push_back({static_cast<size_t>(mi), world, nm && nm->IsStr() ? nm->s : std::string()});
        if (instances.size() > 1'000'000) { bad = true; return; }
      }
      if (const Json* ch = nd.Get("children")) {
        if (!ch->IsArr()) { bad = true; return; }
        on_stack.push_back(ni);
        for (const Json& j : ch->a) { int64_t c; if (!IntOf(&j, c)) { bad = true; return; } visit(c, world, depth + 1); }
        on_stack.pop_back();
      }
    };
    for (int64_t r : roots) visit(r, Affine(), 0);
    if (bad) return Result::Failed;
  }

  std::vector<GltfPart> parts;
  size_t total_tris = 0, total_elements = 0;
  for (const Instance& inst : instances) {
    const Geometry& g = geoms[inst.geometry];
    const NormalMatrix nm = NormalMatrixOf(inst.xf);
    const bool flip = nm.det < 0.0;
    for (const Primitive& prim : g.prims) {
      if (prim.tris.empty()) continue;
      total_elements += prim.verts.size() / 3 + prim.tris.size() / 3;
      if (total_elements > kMaxElements) return Result::Failed;  // instancing multiplies what the file holds
      GltfPart part;
      part.name = !inst.node_name.empty() ? inst.node_name : g.name;
      if (prim.material >= 0) part.material = materials[static_cast<size_t>(prim.material)];
      ON_Mesh& raw = part.mesh.raw();
      raw.m_V.Reserve(prim.verts.size() / 3);
      for (size_t v = 0; v + 2 < prim.verts.size(); v += 3) {
        const double x = prim.verts[v], y = prim.verts[v + 1], z = prim.verts[v + 2];
        double wx = inst.xf.m[0] * x + inst.xf.m[4] * y + inst.xf.m[8] * z + inst.xf.m[12];
        double wy = inst.xf.m[1] * x + inst.xf.m[5] * y + inst.xf.m[9] * z + inst.xf.m[13];
        double wz = inst.xf.m[2] * x + inst.xf.m[6] * y + inst.xf.m[10] * z + inst.xf.m[14];
        if (options.y_up_to_z_up) { const double ty = wy; wy = -wz; wz = ty; }
        const float fx = static_cast<float>(wx * options.scale), fy = static_cast<float>(wy * options.scale), fz = static_cast<float>(wz * options.scale);
        if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz)) return Result::Failed;
        raw.m_V.Append(ON_3fPoint(fx, fy, fz));
      }
      // Normals go with the node by the inverse transpose; a singular node matrix (a zero scale) leaves none to give.
      if (!prim.normals.empty() && std::fabs(nm.det) > 0.0 && std::isfinite(nm.det)) {
        raw.m_N.Reserve(prim.normals.size() / 3);
        const double sign = flip ? -1.0 : 1.0;
        for (size_t v = 0; v + 2 < prim.normals.size(); v += 3) {
          const double x = prim.normals[v], y = prim.normals[v + 1], z = prim.normals[v + 2];
          double n[3];
          for (int k = 0; k < 3; ++k) n[k] = sign * (nm.r[k][0] * x + nm.r[k][1] * y + nm.r[k][2] * z);
          const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
          if (std::isfinite(len) && len > 0.0) { n[0] /= len; n[1] /= len; n[2] /= len; }
          else { n[0] = n[1] = n[2] = 0.0; }  // the file's own normal was zero: kept as "none" rather than invented
          if (options.y_up_to_z_up) { const double ty = n[1]; n[1] = -n[2]; n[2] = ty; }
          raw.m_N.Append(ON_3fVector(static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2])));
        }
      }
      raw.m_F.Reserve(prim.tris.size() / 3);
      for (size_t t = 0; t + 2 < prim.tris.size(); t += 3) {
        ON_MeshFace face;
        face.vi[0] = prim.tris[t];
        face.vi[1] = prim.tris[flip ? t + 2 : t + 1];
        face.vi[2] = prim.tris[flip ? t + 1 : t + 2];
        face.vi[3] = face.vi[2];
        raw.m_F.Append(face);
      }
      total_tris += prim.tris.size() / 3;
      parts.push_back(std::move(part));
    }
  }
  if (parts.empty() || total_tris == 0) return Result::Failed;
  model.parts = std::move(parts);
  return Result::Ok;
}

// The long-standing single-mesh entry points use this implementation.
Result Mesh::SaveGltf(const std::string& path) const {
  GltfModel model;
  GltfPart part;
  part.mesh = *this;
  model.parts.push_back(std::move(part));
  return SaveGltfModel(path, model);
}

Result Mesh::SaveGlb(const std::string& path) const {
  GltfModel model;
  GltfPart part;
  part.mesh = *this;
  model.parts.push_back(std::move(part));
  GltfWriteOptions opt;
  opt.binary = true;
  return SaveGltfModel(path, model, opt);
}

namespace {

Result LoadMerged(const std::string& path, Mesh& out_mesh) {
  GltfModel model;
  if (LoadGltfModel(path, model) != Result::Ok) return Result::Failed;
  Mesh merged;
  ON_Mesh& raw = merged.raw();
  bool all_normals = true;
  for (const GltfPart& part : model.parts) all_normals = all_normals && part.mesh.raw().m_N.Count() == part.mesh.raw().m_V.Count();
  for (const GltfPart& part : model.parts) {
    const ON_Mesh& m = part.mesh.raw();
    const int base = raw.m_V.Count();
    for (int i = 0; i < m.m_V.Count(); ++i) raw.m_V.Append(m.m_V[i]);
    if (all_normals) { for (int i = 0; i < m.m_N.Count(); ++i) raw.m_N.Append(m.m_N[i]); }
    for (int f = 0; f < m.m_F.Count(); ++f) {
      ON_MeshFace face = m.m_F[f];
      for (int k = 0; k < 4; ++k) face.vi[k] += base;
      raw.m_F.Append(face);
    }
  }
  out_mesh = std::move(merged);
  return Result::Ok;
}

}  // namespace

Result Mesh::LoadGltf(const std::string& path, Mesh& out_mesh) { return LoadMerged(path, out_mesh); }
Result Mesh::LoadGlb(const std::string& path, Mesh& out_mesh) { return LoadMerged(path, out_mesh); }

}  // namespace dino8::kernel
