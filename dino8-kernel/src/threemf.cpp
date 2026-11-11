#include "dino8/kernel/threemf.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

#include "zlib.h"

namespace dino8::kernel {

namespace {

constexpr size_t kMaxEntryBytes = size_t{1} << 30;  // 1 GiB per entry
constexpr size_t kMaxElements = 100'000'000;        // vertices + triangles over the whole file
constexpr int kMaxXmlDepth = 100;
constexpr int kMaxComponentDepth = 32;

// ---------------------------------------------------------------------------------------------------- ZIP

struct ZipEntryInfo {
  uint16_t method = 0;
  uint32_t crc = 0;
  uint32_t comp_size = 0;
  uint32_t uncomp_size = 0;
  uint32_t local_offset = 0;
};

uint16_t U16(const std::string& d, size_t at) {
  return static_cast<uint16_t>(static_cast<unsigned char>(d[at]) | (static_cast<unsigned char>(d[at + 1]) << 8));
}
uint32_t U32(const std::string& d, size_t at) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(d[at + static_cast<size_t>(i)]);
  return v;
}

struct ZipReader {
  std::string data;
  std::map<std::string, ZipEntryInfo> entries;

  bool Open(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (data.size() < 22) return false;
    // End of central directory: scan backwards (a comment of up to 65535 bytes may follow it).
    size_t eocd = std::string::npos;
    const size_t lowest = data.size() > 22 + 65535 ? data.size() - 22 - 65535 : 0;
    for (size_t i = data.size() - 22;; --i) {
      if (U32(data, i) == 0x06054b50u) { eocd = i; break; }
      if (i == lowest) break;
    }
    if (eocd == std::string::npos) return false;
    const uint16_t count = U16(data, eocd + 10);
    const uint32_t cd_size = U32(data, eocd + 12);
    const uint32_t cd_offset = U32(data, eocd + 16);
    if (count == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_offset == 0xFFFFFFFFu) return false;  // ZIP64
    if (static_cast<uint64_t>(cd_offset) + cd_size > data.size()) return false;
    size_t pos = cd_offset;
    for (uint16_t i = 0; i < count; ++i) {
      if (pos + 46 > data.size() || U32(data, pos) != 0x02014b50u) return false;
      const uint16_t flags = U16(data, pos + 8);
      ZipEntryInfo e;
      e.method = U16(data, pos + 10);
      e.crc = U32(data, pos + 16);
      e.comp_size = U32(data, pos + 20);
      e.uncomp_size = U32(data, pos + 24);
      const uint16_t name_len = U16(data, pos + 28), extra_len = U16(data, pos + 30), comment_len = U16(data, pos + 32);
      e.local_offset = U32(data, pos + 42);
      if (pos + 46 + name_len > data.size()) return false;
      if (flags & 1u) return false;  // encrypted
      std::string name = data.substr(pos + 46, name_len);
      pos += 46 + static_cast<size_t>(name_len) + extra_len + comment_len;
      if (!name.empty() && name.back() == '/') continue;  // directory entry
      if (!name.empty() && name[0] == '/') name.erase(0, 1);
      entries[name] = e;
    }
    return true;
  }

  bool Has(const std::string& name) const { return entries.count(name) != 0; }

  // Extracts `name` into `out`: stored or deflate, size- and CRC-checked.
  bool Read(const std::string& name, std::string& out) const {
    const auto it = entries.find(name);
    if (it == entries.end()) return false;
    const ZipEntryInfo& e = it->second;
    if (e.uncomp_size > kMaxEntryBytes || e.comp_size == 0xFFFFFFFFu || e.uncomp_size == 0xFFFFFFFFu) return false;
    if (static_cast<uint64_t>(e.local_offset) + 30 > data.size() || U32(data, e.local_offset) != 0x04034b50u) return false;
    const size_t start = static_cast<size_t>(e.local_offset) + 30 + U16(data, e.local_offset + 26) + U16(data, e.local_offset + 28);
    if (start > data.size() || data.size() - start < e.comp_size) return false;
    const unsigned char* src = reinterpret_cast<const unsigned char*>(data.data()) + start;
    std::string result;
    if (e.method == 0) {
      if (e.comp_size != e.uncomp_size) return false;
      result.assign(reinterpret_cast<const char*>(src), e.comp_size);
    } else if (e.method == 8) {
      result.resize(e.uncomp_size);
      z_stream zs;
      std::memset(&zs, 0, sizeof(zs));
      if (inflateInit2(&zs, -15) != Z_OK) return false;
      zs.next_in = const_cast<unsigned char*>(src);
      zs.avail_in = e.comp_size;
      zs.next_out = reinterpret_cast<unsigned char*>(result.data());
      zs.avail_out = e.uncomp_size;
      const int rc = inflate(&zs, Z_FINISH);
      const bool exact = rc == Z_STREAM_END && zs.total_out == e.uncomp_size;
      inflateEnd(&zs);
      if (!exact) return false;
    } else {
      return false;
    }
    const uLong crc = crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(result.data()), static_cast<uInt>(result.size()));
    if (static_cast<uint32_t>(crc) != e.crc) return false;
    out = std::move(result);
    return true;
  }
};

void Put16(std::string& s, uint16_t v) { s.push_back(static_cast<char>(v & 0xFF)); s.push_back(static_cast<char>(v >> 8)); }
void Put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); }

struct ZipPart { std::string name; std::string data; };

bool WriteZip(const std::string& path, const std::vector<ZipPart>& parts, bool compress) {
  std::string out;
  struct Central { std::string name; uint16_t method; uint32_t crc, comp, uncomp, offset; };
  std::vector<Central> central;
  for (const ZipPart& p : parts) {
    if (p.data.size() >= 0xFFFFFFFFull || out.size() >= 0xFFFFFFFFull - 1000) return false;
    Central c;
    c.name = p.name;
    c.crc = static_cast<uint32_t>(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(p.data.data()), static_cast<uInt>(p.data.size())));
    c.uncomp = static_cast<uint32_t>(p.data.size());
    std::string body;
    c.method = 0;
    if (compress && !p.data.empty()) {
      z_stream zs;
      std::memset(&zs, 0, sizeof(zs));
      if (deflateInit2(&zs, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
      body.resize(deflateBound(&zs, static_cast<uLong>(p.data.size())));
      zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(p.data.data()));
      zs.avail_in = static_cast<uInt>(p.data.size());
      zs.next_out = reinterpret_cast<Bytef*>(body.data());
      zs.avail_out = static_cast<uInt>(body.size());
      const int rc = deflate(&zs, Z_FINISH);
      const size_t produced = zs.total_out;
      deflateEnd(&zs);
      if (rc != Z_STREAM_END) return false;
      body.resize(produced);
      if (body.size() < p.data.size()) c.method = 8;
    }
    if (c.method == 0) body = p.data;
    c.comp = static_cast<uint32_t>(body.size());
    c.offset = static_cast<uint32_t>(out.size());
    Put32(out, 0x04034b50u); Put16(out, 20); Put16(out, 0); Put16(out, c.method); Put16(out, 0); Put16(out, 0x21);
    Put32(out, c.crc); Put32(out, c.comp); Put32(out, c.uncomp);
    Put16(out, static_cast<uint16_t>(c.name.size())); Put16(out, 0);
    out += c.name;
    out += body;
    central.push_back(c);
  }
  const uint32_t cd_offset = static_cast<uint32_t>(out.size());
  for (const Central& c : central) {
    Put32(out, 0x02014b50u); Put16(out, 20); Put16(out, 20); Put16(out, 0); Put16(out, c.method); Put16(out, 0); Put16(out, 0x21);
    Put32(out, c.crc); Put32(out, c.comp); Put32(out, c.uncomp);
    Put16(out, static_cast<uint16_t>(c.name.size())); Put16(out, 0); Put16(out, 0); Put16(out, 0); Put16(out, 0);
    Put32(out, 0); Put32(out, c.offset);
    out += c.name;
  }
  const uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_offset;
  Put32(out, 0x06054b50u); Put16(out, 0); Put16(out, 0); Put16(out, static_cast<uint16_t>(central.size())); Put16(out, static_cast<uint16_t>(central.size()));
  Put32(out, cd_size); Put32(out, cd_offset); Put16(out, 0);
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(out.data(), static_cast<std::streamsize>(out.size()));
  return f.good();
}

// ---------------------------------------------------------------------------------------------------- XML (SAX)

struct XmlAttr { std::string name, value; };

std::string LocalName(const std::string& qualified) {
  const size_t colon = qualified.rfind(':');
  return colon == std::string::npos ? qualified : qualified.substr(colon + 1);
}

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool DecodeEntities(const std::string& in, std::string& out) {
  out.clear();
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '&') { out.push_back(in[i]); continue; }
    const size_t semi = in.find(';', i);
    if (semi == std::string::npos || semi - i > 12) return false;
    const std::string ent = in.substr(i + 1, semi - i - 1);
    if (ent == "amp") out.push_back('&');
    else if (ent == "lt") out.push_back('<');
    else if (ent == "gt") out.push_back('>');
    else if (ent == "quot") out.push_back('"');
    else if (ent == "apos") out.push_back('\'');
    else if (ent.size() > 1 && ent[0] == '#') {
      unsigned long cp = 0;
      const bool hex = ent[1] == 'x' || ent[1] == 'X';
      const char* b = ent.data() + (hex ? 2 : 1);
      const char* e = ent.data() + ent.size();
      if (b == e) return false;
      const auto r = std::from_chars(b, e, cp, hex ? 16 : 10);
      if (r.ec != std::errc() || r.ptr != e || cp == 0 || cp > 0x10FFFF) return false;
      // UTF-8 encode.
      if (cp < 0x80) out.push_back(static_cast<char>(cp));
      else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
      else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
      else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    } else {
      return false;  // only the five predefined entities and numeric references; no DTD-defined entities
    }
    i = semi;
  }
  return true;
}

using XmlStart = std::function<bool(const std::string& local, const std::vector<XmlAttr>& attrs)>;
using XmlEnd = std::function<bool(const std::string& local)>;

// A small, strict SAX scan: elements, attributes, comments, CDATA, processing instructions; no DOCTYPE (so no entity tricks).
// A self-closing element calls `start` then `end`. Returns false on any malformation or when a callback returns false.
bool ParseXml(const std::string& s, const XmlStart& start, const XmlEnd& end) {
  size_t i = 0;
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) i = 3;
  std::vector<std::string> stack;
  bool saw_root = false;
  while (i < s.size()) {
    if (s[i] != '<') {
      if (!IsSpace(s[i]) && stack.empty()) return false;  // text outside the root element
      ++i;
      continue;
    }
    if (s.compare(i, 4, "<!--") == 0) {
      const size_t e = s.find("-->", i + 4);
      if (e == std::string::npos) return false;
      i = e + 3;
      continue;
    }
    if (s.compare(i, 9, "<![CDATA[") == 0) {
      const size_t e = s.find("]]>", i + 9);
      if (e == std::string::npos || stack.empty()) return false;
      i = e + 3;
      continue;
    }
    if (s.compare(i, 2, "<?") == 0) {
      const size_t e = s.find("?>", i + 2);
      if (e == std::string::npos) return false;
      i = e + 2;
      continue;
    }
    if (s.compare(i, 2, "<!") == 0) return false;  // DOCTYPE / ENTITY declarations are refused
    if (s.compare(i, 2, "</") == 0) {
      const size_t e = s.find('>', i + 2);
      if (e == std::string::npos) return false;
      std::string name = s.substr(i + 2, e - i - 2);
      while (!name.empty() && IsSpace(name.back())) name.pop_back();
      if (stack.empty() || stack.back() != name) return false;
      stack.pop_back();
      if (!end(LocalName(name))) return false;
      i = e + 1;
      continue;
    }
    // Start tag.
    size_t p = i + 1;
    const size_t name_begin = p;
    while (p < s.size() && !IsSpace(s[p]) && s[p] != '/' && s[p] != '>') ++p;
    if (p == name_begin || p >= s.size()) return false;
    const std::string name = s.substr(name_begin, p - name_begin);
    std::vector<XmlAttr> attrs;
    bool self_closing = false;
    while (true) {
      while (p < s.size() && IsSpace(s[p])) ++p;
      if (p >= s.size()) return false;
      if (s[p] == '>') { ++p; break; }
      if (s[p] == '/') {
        if (p + 1 >= s.size() || s[p + 1] != '>') return false;
        self_closing = true;
        p += 2;
        break;
      }
      const size_t an = p;
      while (p < s.size() && s[p] != '=' && !IsSpace(s[p]) && s[p] != '>' && s[p] != '/') ++p;
      if (p == an || p >= s.size()) return false;
      const std::string aname = s.substr(an, p - an);
      while (p < s.size() && IsSpace(s[p])) ++p;
      if (p >= s.size() || s[p] != '=') return false;
      ++p;
      while (p < s.size() && IsSpace(s[p])) ++p;
      if (p >= s.size() || (s[p] != '"' && s[p] != '\'')) return false;
      const char quote = s[p++];
      const size_t vb = p;
      while (p < s.size() && s[p] != quote) {
        if (s[p] == '<') return false;
        ++p;
      }
      if (p >= s.size()) return false;
      XmlAttr a;
      a.name = LocalName(aname);
      if (!DecodeEntities(s.substr(vb, p - vb), a.value)) return false;
      attrs.push_back(std::move(a));
      ++p;
    }
    if (stack.empty() && saw_root) return false;  // a second root element
    saw_root = true;
    if (static_cast<int>(stack.size()) >= kMaxXmlDepth) return false;
    if (!start(LocalName(name), attrs)) return false;
    if (self_closing) {
      if (!end(LocalName(name))) return false;
    } else {
      stack.push_back(name);
    }
    i = p;
  }
  return saw_root && stack.empty();
}

const std::string* Attr(const std::vector<XmlAttr>& attrs, const char* name) {
  for (const XmlAttr& a : attrs) {
    if (a.name == name) return &a.value;
  }
  return nullptr;
}

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && IsSpace(s[b])) ++b;
  while (e > b && IsSpace(s[e - 1])) --e;
  return s.substr(b, e - b);
}

bool ParseDouble(const std::string& text, double& out) {
  const std::string t = Trim(text);
  if (t.empty()) return false;
  const char* b = t.data() + (t[0] == '+' ? 1 : 0);
  const char* e = t.data() + t.size();
  const auto r = std::from_chars(b, e, out);
  return r.ec == std::errc() && r.ptr == e && std::isfinite(out);
}

bool ParseId(const std::string& text, uint32_t& out) {
  const std::string t = Trim(text);
  if (t.empty()) return false;
  const auto r = std::from_chars(t.data(), t.data() + t.size(), out);
  return r.ec == std::errc() && r.ptr == t.data() + t.size();
}

// ---------------------------------------------------------------------------------------------------- model

// Row-vector affine map p' = p R + t (3MF's own convention: the 12 numbers are m00 m01 m02 m10 .. m22 m30 m31 m32).
struct Affine {
  double r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  double t[3] = {0, 0, 0};
};

// "a then b": p -> (p a) b.
Affine Then(const Affine& a, const Affine& b) {
  Affine c;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) c.r[i][j] = a.r[i][0] * b.r[0][j] + a.r[i][1] * b.r[1][j] + a.r[i][2] * b.r[2][j];
  }
  for (int j = 0; j < 3; ++j) c.t[j] = a.t[0] * b.r[0][j] + a.t[1] * b.r[1][j] + a.t[2] * b.r[2][j] + b.t[j];
  return c;
}

double Det(const Affine& a) {
  return a.r[0][0] * (a.r[1][1] * a.r[2][2] - a.r[1][2] * a.r[2][1]) - a.r[0][1] * (a.r[1][0] * a.r[2][2] - a.r[1][2] * a.r[2][0]) +
         a.r[0][2] * (a.r[1][0] * a.r[2][1] - a.r[1][1] * a.r[2][0]);
}

bool ParseTransform(const std::string& text, Affine& out) {
  std::istringstream in(text);
  std::vector<double> v;
  std::string tok;
  while (in >> tok) {
    double d = 0;
    if (!ParseDouble(tok, d)) return false;
    v.push_back(d);
  }
  if (v.size() != 12) return false;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) out.r[i][j] = v[static_cast<size_t>(i * 3 + j)];
  }
  for (int j = 0; j < 3; ++j) out.t[j] = v[static_cast<size_t>(9 + j)];
  return true;
}

struct Component {
  uint32_t object = 0;
  Affine xf;
};

struct ObjectRec {
  std::string name;
  std::vector<float> verts;       // x y z ...
  std::vector<int32_t> tris;      // v1 v2 v3 ...
  std::vector<Component> comps;
};

struct BuildItem {
  uint32_t object = 0;
  Affine xf;
};

double UnitMm(const std::string& unit) {
  if (unit == "micron") return 0.001;
  if (unit == "millimeter") return 1.0;
  if (unit == "centimeter") return 10.0;
  if (unit == "inch") return 25.4;
  if (unit == "foot") return 304.8;
  if (unit == "meter") return 1000.0;
  return 0.0;
}

std::string XmlEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&apos;"; break;
      default: o.push_back(c);
    }
  }
  return o;
}

void AppendFloat(std::string& s, float v) {
  char buf[48];
  const auto r = std::to_chars(buf, buf + sizeof(buf), v);
  s.append(buf, r.ptr);
}

}  // namespace

double ThreeMfUnitToMillimeters(const std::string& unit) { return UnitMm(unit); }

Result SaveThreeMf(const std::string& path, const ThreeMfModel& model, const ThreeMfWriteOptions& options) {
  if (UnitMm(model.unit) == 0.0 || model.parts.empty()) return Result::Failed;
  std::string xml;
  xml.reserve(1 << 16);
  xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  xml += "<model unit=\"" + model.unit + "\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n";
  xml += " <resources>\n";
  int id = 1;
  for (const ThreeMfPart& part : model.parts) {
    const ON_Mesh& m = part.mesh.raw();
    const int nv = m.m_V.Count();
    if (nv <= 0 || m.m_F.Count() <= 0) return Result::Failed;
    xml += "  <object id=\"" + std::to_string(id++) + "\" type=\"model\"";
    if (!part.name.empty()) xml += " name=\"" + XmlEscape(part.name) + "\"";
    xml += ">\n   <mesh>\n    <vertices>\n";
    for (int i = 0; i < nv; ++i) {
      const ON_3fPoint& v = m.m_V[i];
      if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return Result::Failed;
      xml += "     <vertex x=\"";
      AppendFloat(xml, v.x);
      xml += "\" y=\"";
      AppendFloat(xml, v.y);
      xml += "\" z=\"";
      AppendFloat(xml, v.z);
      xml += "\"/>\n";
    }
    xml += "    </vertices>\n    <triangles>\n";
    bool bad_index = false;
    const auto tri = [&](int a, int b, int c) {
      if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) { bad_index = true; return; }
      xml += "     <triangle v1=\"" + std::to_string(a) + "\" v2=\"" + std::to_string(b) + "\" v3=\"" + std::to_string(c) + "\"/>\n";
    };
    for (int f = 0; f < m.m_F.Count(); ++f) {
      const ON_MeshFace& face = m.m_F[f];
      tri(face.vi[0], face.vi[1], face.vi[2]);
      if (face.IsQuad()) tri(face.vi[0], face.vi[2], face.vi[3]);
    }
    if (bad_index) return Result::Failed;
    xml += "    </triangles>\n   </mesh>\n  </object>\n";
  }
  xml += " </resources>\n <build>\n";
  for (size_t k = 0; k < model.parts.size(); ++k) xml += "  <item objectid=\"" + std::to_string(k + 1) + "\"/>\n";
  xml += " </build>\n</model>\n";

  const std::vector<ZipPart> parts = {
      {"[Content_Types].xml",
       "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
       " <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
       " <Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
       "</Types>\n"},
      {"_rels/.rels",
       "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
       " <Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n"
       "</Relationships>\n"},
      {"3D/3dmodel.model", xml},
  };
  return WriteZip(path, parts, options.compress_entries) ? Result::Ok : Result::Failed;
}

Result LoadThreeMf(const std::string& path, ThreeMfModel& model, const ThreeMfReadOptions& options) {
  ZipReader zip;
  if (!zip.Open(path)) return Result::Failed;

  // Model part: through the package relationships, else the conventional location.
  std::string model_name = "3D/3dmodel.model";
  if (zip.Has("_rels/.rels")) {
    std::string rels;
    if (!zip.Read("_rels/.rels", rels)) return Result::Failed;
    std::string target;
    const bool ok = ParseXml(
        rels,
        [&](const std::string& n, const std::vector<XmlAttr>& a) {
          if (n == "Relationship" && target.empty()) {
            const std::string* type = Attr(a, "Type");
            const std::string* tgt = Attr(a, "Target");
            if (type && tgt && type->size() >= 8 && type->compare(type->size() - 8, 8, "/3dmodel") == 0) target = *tgt;
          }
          return true;
        },
        [](const std::string&) { return true; });
    if (!ok) return Result::Failed;
    if (!target.empty()) {
      if (target[0] == '/') target.erase(0, 1);
      if (zip.Has(target)) model_name = target;
    }
  }
  std::string xml;
  if (!zip.Read(model_name, xml)) return Result::Failed;

  std::string unit = "millimeter";
  std::map<uint32_t, ObjectRec> objects;
  std::vector<BuildItem> items;
  bool saw_build = false;
  ObjectRec* cur = nullptr;
  std::vector<std::string> path_stack;
  size_t element_count = 0;
  bool bad = false;

  const bool parsed = ParseXml(
      xml,
      [&](const std::string& n, const std::vector<XmlAttr>& a) {
        const std::string parent = path_stack.empty() ? std::string() : path_stack.back();
        path_stack.push_back(n);
        if (path_stack.size() == 1) {
          if (n != "model") return false;
          if (const std::string* u = Attr(a, "unit")) unit = Trim(*u);
          return UnitMm(unit) != 0.0;
        }
        if (n == "object" && parent == "resources") {
          uint32_t id = 0;
          const std::string* ids = Attr(a, "id");
          if (!ids || !ParseId(*ids, id) || objects.count(id)) return false;
          cur = &objects[id];
          if (const std::string* nm = Attr(a, "name")) cur->name = *nm;
          return true;
        }
        if (n == "vertex" && parent == "vertices" && cur) {
          const std::string *x = Attr(a, "x"), *y = Attr(a, "y"), *z = Attr(a, "z");
          double dx = 0, dy = 0, dz = 0;
          if (!x || !y || !z || !ParseDouble(*x, dx) || !ParseDouble(*y, dy) || !ParseDouble(*z, dz)) return false;
          if (++element_count > kMaxElements) return false;
          cur->verts.push_back(static_cast<float>(dx));
          cur->verts.push_back(static_cast<float>(dy));
          cur->verts.push_back(static_cast<float>(dz));
          return true;
        }
        if (n == "triangle" && parent == "triangles" && cur) {
          const std::string *v1 = Attr(a, "v1"), *v2 = Attr(a, "v2"), *v3 = Attr(a, "v3");
          uint32_t i1 = 0, i2 = 0, i3 = 0;
          if (!v1 || !v2 || !v3 || !ParseId(*v1, i1) || !ParseId(*v2, i2) || !ParseId(*v3, i3)) return false;
          if (i1 > 0x7FFFFFFFu || i2 > 0x7FFFFFFFu || i3 > 0x7FFFFFFFu) return false;
          if (++element_count > kMaxElements) return false;
          cur->tris.push_back(static_cast<int32_t>(i1));
          cur->tris.push_back(static_cast<int32_t>(i2));
          cur->tris.push_back(static_cast<int32_t>(i3));
          return true;
        }
        if (n == "component" && parent == "components" && cur) {
          if (const std::string* p = Attr(a, "path")) {
            if (!p->empty()) return false;  // external-part (production extension) components are not read
          }
          Component c;
          const std::string* oid = Attr(a, "objectid");
          if (!oid || !ParseId(*oid, c.object)) return false;
          if (const std::string* t = Attr(a, "transform")) {
            if (!ParseTransform(*t, c.xf)) return false;
          }
          cur->comps.push_back(c);
          return true;
        }
        if (n == "build" && parent == "model") { saw_build = true; return true; }
        if (n == "item" && parent == "build") {
          BuildItem it;
          const std::string* oid = Attr(a, "objectid");
          if (!oid || !ParseId(*oid, it.object)) return false;
          if (const std::string* t = Attr(a, "transform")) {
            if (!ParseTransform(*t, it.xf)) return false;
          }
          items.push_back(it);
          return true;
        }
        return true;  // metadata, extension elements, ...
      },
      [&](const std::string& n) {
        if (!path_stack.empty()) path_stack.pop_back();
        if (n == "mesh" && cur) {
          const int nv = static_cast<int>(cur->verts.size() / 3);
          for (int32_t idx : cur->tris) {
            if (idx >= nv) bad = true;
          }
        }
        if (n == "object") cur = nullptr;
        return !bad;
      });
  if (!parsed || bad) return Result::Failed;

  if (!saw_build || items.empty()) {
    // No build list: build every object that no other object uses as a component.
    std::set<uint32_t> used;
    for (const auto& kv : objects) {
      for (const Component& c : kv.second.comps) used.insert(c.object);
    }
    items.clear();
    for (const auto& kv : objects) {
      if (!used.count(kv.first)) items.push_back(BuildItem{kv.first, Affine()});
    }
  }

  const double scale = options.convert_to_millimeters ? UnitMm(unit) : 1.0;
  std::vector<ThreeMfPart> parts;
  size_t total_tris = 0;
  bool failed = false;
  for (const BuildItem& item : items) {
    ThreeMfPart part;
    ON_Mesh& raw = part.mesh.raw();
    std::set<uint32_t> on_stack;
    std::function<void(uint32_t, const Affine&, int)> flatten = [&](uint32_t id, const Affine& xf, int depth) {
      if (failed) return;
      const auto it = objects.find(id);
      if (it == objects.end() || depth > kMaxComponentDepth || on_stack.count(id)) { failed = true; return; }
      const ObjectRec& obj = it->second;
      if (part.name.empty() && depth == 0) part.name = obj.name;
      if (!obj.tris.empty()) {
        const int base = raw.m_V.Count();
        const bool flip = Det(xf) < 0.0;
        for (size_t v = 0; v + 2 < obj.verts.size(); v += 3) {
          const double x = obj.verts[v], y = obj.verts[v + 1], z = obj.verts[v + 2];
          const double wx = (x * xf.r[0][0] + y * xf.r[1][0] + z * xf.r[2][0] + xf.t[0]) * scale;
          const double wy = (x * xf.r[0][1] + y * xf.r[1][1] + z * xf.r[2][1] + xf.t[1]) * scale;
          const double wz = (x * xf.r[0][2] + y * xf.r[1][2] + z * xf.r[2][2] + xf.t[2]) * scale;
          raw.m_V.Append(ON_3fPoint(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz)));
        }
        for (size_t t = 0; t + 2 < obj.tris.size(); t += 3) {
          ON_MeshFace face;
          face.vi[0] = base + obj.tris[t];
          face.vi[1] = base + obj.tris[flip ? t + 2 : t + 1];
          face.vi[2] = base + obj.tris[flip ? t + 1 : t + 2];
          face.vi[3] = face.vi[2];
          raw.m_F.Append(face);
        }
      }
      on_stack.insert(id);
      for (const Component& c : obj.comps) flatten(c.object, Then(c.xf, xf), depth + 1);
      on_stack.erase(id);
    };
    flatten(item.object, item.xf, 0);
    if (failed) return Result::Failed;
    if (raw.m_F.Count() == 0) continue;  // an assembly that resolved to no geometry
    total_tris += static_cast<size_t>(raw.m_F.Count());
    parts.push_back(std::move(part));
  }
  if (parts.empty() || total_tris == 0) return Result::Failed;

  model.unit = options.convert_to_millimeters ? "millimeter" : unit;
  model.parts = std::move(parts);
  return Result::Ok;
}

// The long-standing single-mesh entry points use this implementation (see las.cpp for the same pattern).
Result Mesh::Save3mf(const std::string& path) const {
  ThreeMfModel model;
  ThreeMfPart part;
  part.mesh = *this;
  model.parts.push_back(std::move(part));
  return SaveThreeMf(path, model);
}

Result Mesh::Load3mf(const std::string& path, Mesh& out_mesh) {
  ThreeMfModel model;
  if (LoadThreeMf(path, model) != Result::Ok) return Result::Failed;
  Mesh merged;
  ON_Mesh& raw = merged.raw();
  for (const ThreeMfPart& part : model.parts) {
    const ON_Mesh& m = part.mesh.raw();
    const int base = raw.m_V.Count();
    for (int i = 0; i < m.m_V.Count(); ++i) raw.m_V.Append(m.m_V[i]);
    for (int f = 0; f < m.m_F.Count(); ++f) {
      ON_MeshFace face = m.m_F[f];
      for (int k = 0; k < 4; ++k) face.vi[k] += base;
      raw.m_F.Append(face);
    }
  }
  out_mesh = std::move(merged);
  return Result::Ok;
}

}  // namespace dino8::kernel
