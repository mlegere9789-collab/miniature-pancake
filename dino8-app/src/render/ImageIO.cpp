#include "render/ImageIO.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace dino8::app {

namespace {

bool ReadFile(const std::string& path, std::vector<unsigned char>& out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < 0) { std::fclose(f); return false; }
  out.resize(static_cast<size_t>(n));
  const size_t got = n > 0 ? std::fread(out.data(), 1, out.size(), f) : 0;
  std::fclose(f);
  return got == out.size();
}

std::string LowerExt(const std::string& path) {
  std::string e = std::filesystem::path(path).extension().string();
  for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

// ---------------------------------------------------------------------------
// PPM / PGM (P2, P3, P5, P6)
// ---------------------------------------------------------------------------

bool LoadPpm(const std::vector<unsigned char>& d, Image& img, std::string& error) {
  size_t pos = 0;
  auto skip = [&]() {
    while (pos < d.size()) {
      if (std::isspace(d[pos])) ++pos;
      else if (d[pos] == '#') { while (pos < d.size() && d[pos] != '\n') ++pos; }
      else break;
    }
  };
  auto number = [&](int& v) {
    skip();
    if (pos >= d.size() || !std::isdigit(d[pos])) return false;
    v = 0;
    while (pos < d.size() && std::isdigit(d[pos])) v = v * 10 + (d[pos++] - '0');
    return true;
  };
  if (d.size() < 2 || d[0] != 'P' || d[1] < '2' || d[1] > '6' || d[1] == '4') { error = "not a P2/P3/P5/P6 PPM"; return false; }
  const char kind = static_cast<char>(d[1]);
  pos = 2;
  int w, h, maxv;
  if (!number(w) || !number(h) || !number(maxv) || w <= 0 || h <= 0 || maxv <= 0) { error = "bad PPM header"; return false; }
  ++pos;  // single whitespace after maxval
  const int channels = (kind == '3' || kind == '6') ? 3 : 1;
  img.width = w; img.height = h;
  img.rgba.assign(static_cast<size_t>(w) * h * 4, 255);
  const bool binary = kind == '5' || kind == '6';
  const bool wide = maxv > 255;
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    int c[3] = {0, 0, 0};
    for (int k = 0; k < channels; ++k) {
      int v = 0;
      if (binary) {
        if (wide) { if (pos + 1 >= d.size()) { error = "truncated PPM"; return false; } v = (d[pos] << 8) | d[pos + 1]; pos += 2; }
        else { if (pos >= d.size()) { error = "truncated PPM"; return false; } v = d[pos++]; }
      } else if (!number(v)) { error = "truncated PPM"; return false; }
      c[k] = v * 255 / maxv;
    }
    unsigned char* px = &img.rgba[i * 4];
    px[0] = static_cast<unsigned char>(c[0]);
    px[1] = static_cast<unsigned char>(channels == 3 ? c[1] : c[0]);
    px[2] = static_cast<unsigned char>(channels == 3 ? c[2] : c[0]);
  }
  return true;
}

// ---------------------------------------------------------------------------
// BMP (BITMAPINFOHEADER, 24/32-bit, uncompressed or BI_BITFIELDS)
// ---------------------------------------------------------------------------

bool LoadBmp(const std::vector<unsigned char>& d, Image& img, std::string& error) {
  auto u16 = [&](size_t at) { return static_cast<unsigned>(d[at]) | (static_cast<unsigned>(d[at + 1]) << 8); };
  auto u32 = [&](size_t at) { return u16(at) | (u16(at + 2) << 16); };
  if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') { error = "not a BMP"; return false; }
  const unsigned offset = u32(10), header = u32(14);
  if (header < 40) { error = "unsupported BMP header"; return false; }
  const int w = static_cast<int>(u32(18));
  int h = static_cast<int>(u32(22));
  const unsigned bpp = u16(28), compression = u32(30);
  const bool top_down = h < 0;
  if (top_down) h = -h;
  if (w <= 0 || h <= 0) { error = "bad BMP size"; return false; }
  if ((bpp != 24 && bpp != 32) || (compression != 0 && compression != 3)) { error = "only 24/32-bit uncompressed BMP files are supported"; return false; }
  const size_t row = (static_cast<size_t>(w) * bpp / 8 + 3) & ~static_cast<size_t>(3);
  if (offset + row * h > d.size()) { error = "truncated BMP"; return false; }
  img.width = w; img.height = h;
  img.rgba.assign(static_cast<size_t>(w) * h * 4, 255);
  for (int y = 0; y < h; ++y) {
    const unsigned char* src = &d[offset + row * static_cast<size_t>(top_down ? y : (h - 1 - y))];
    for (int x = 0; x < w; ++x) {
      unsigned char* px = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
      const unsigned char* s = src + static_cast<size_t>(x) * bpp / 8;
      px[0] = s[2]; px[1] = s[1]; px[2] = s[0];
      if (bpp == 32) px[3] = s[3] ? s[3] : 255;  // many writers leave alpha 0
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// zlib inflate (RFC 1951) - a compact "puff"-style decoder.
// ---------------------------------------------------------------------------

struct BitReader {
  const unsigned char* data;
  size_t size, pos = 0;
  unsigned bitbuf = 0;
  int bitcnt = 0;
  bool overrun = false;
  int Bits(int need) {
    unsigned val = bitbuf;
    while (bitcnt < need) {
      if (pos >= size) { overrun = true; return 0; }
      val |= static_cast<unsigned>(data[pos++]) << bitcnt;
      bitcnt += 8;
    }
    bitbuf = val >> need;
    bitcnt -= need;
    return static_cast<int>(val & ((1u << need) - 1));
  }
};

struct Huffman {
  short count[16] = {};
  short symbol[320] = {};
  // Builds canonical codes; returns false for an over-subscribed set.
  bool Build(const short* length, int n) {
    for (int i = 0; i < 16; ++i) count[i] = 0;
    for (int i = 0; i < n; ++i) count[length[i]]++;
    if (count[0] == n) return true;
    int left = 1;
    for (int len = 1; len < 16; ++len) { left <<= 1; left -= count[len]; if (left < 0) return false; }
    short offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<short>(offs[len] + count[len]);
    for (int i = 0; i < n; ++i) if (length[i] != 0) symbol[offs[length[i]]++] = static_cast<short>(i);
    return true;
  }
  int Decode(BitReader& br) const {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
      code |= br.Bits(1);
      const int c = count[len];
      if (code - c < first) return symbol[index + (code - first)];
      index += c;
      first += c;
      first <<= 1;
      code <<= 1;
      if (br.overrun) return -1;
    }
    return -1;
  }
};

const short kLenBase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLenExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDistExtra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool InflateCodes(BitReader& br, std::vector<unsigned char>& out, const Huffman& lencode, const Huffman& distcode) {
  for (;;) {
    int sym = lencode.Decode(br);
    if (sym < 0) return false;
    if (sym < 256) { out.push_back(static_cast<unsigned char>(sym)); continue; }
    if (sym == 256) return true;
    sym -= 257;
    if (sym >= 29) return false;
    const int len = kLenBase[sym] + br.Bits(kLenExtra[sym]);
    const int dsym = distcode.Decode(br);
    if (dsym < 0 || dsym >= 30) return false;
    const size_t dist = static_cast<size_t>(kDistBase[dsym] + br.Bits(kDistExtra[dsym]));
    if (br.overrun || dist > out.size()) return false;
    const size_t start = out.size() - dist;
    for (int i = 0; i < len; ++i) out.push_back(out[start + static_cast<size_t>(i)]);
  }
}

bool InflateStored(BitReader& br, std::vector<unsigned char>& out) {
  br.bitbuf = 0; br.bitcnt = 0;  // drop to a byte boundary
  if (br.pos + 4 > br.size) return false;
  const unsigned len = br.data[br.pos] | (br.data[br.pos + 1] << 8);
  const unsigned nlen = br.data[br.pos + 2] | (br.data[br.pos + 3] << 8);
  br.pos += 4;
  if ((len ^ 0xffffu) != nlen || br.pos + len > br.size) return false;
  out.insert(out.end(), br.data + br.pos, br.data + br.pos + len);
  br.pos += len;
  return true;
}

bool InflateFixed(BitReader& br, std::vector<unsigned char>& out) {
  static Huffman lencode, distcode;
  static bool built = false;
  if (!built) {
    short lengths[288];
    int i = 0;
    for (; i < 144; ++i) lengths[i] = 8;
    for (; i < 256; ++i) lengths[i] = 9;
    for (; i < 280; ++i) lengths[i] = 7;
    for (; i < 288; ++i) lengths[i] = 8;
    lencode.Build(lengths, 288);
    for (i = 0; i < 30; ++i) lengths[i] = 5;
    distcode.Build(lengths, 30);
    built = true;
  }
  return InflateCodes(br, out, lencode, distcode);
}

bool InflateDynamic(BitReader& br, std::vector<unsigned char>& out) {
  static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  const int nlen = br.Bits(5) + 257, ndist = br.Bits(5) + 1, ncode = br.Bits(4) + 4;
  if (nlen > 286 || ndist > 30 || br.overrun) return false;
  short lengths[320] = {};
  for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<short>(br.Bits(3));
  Huffman lencode, distcode;
  if (!lencode.Build(lengths, 19)) return false;
  int index = 0;
  while (index < nlen + ndist) {
    int sym = lencode.Decode(br);
    if (sym < 0) return false;
    if (sym < 16) { lengths[index++] = static_cast<short>(sym); continue; }
    int len = 0, rep;
    if (sym == 16) { if (index == 0) return false; len = lengths[index - 1]; rep = 3 + br.Bits(2); }
    else if (sym == 17) rep = 3 + br.Bits(3);
    else rep = 11 + br.Bits(7);
    if (index + rep > nlen + ndist) return false;
    while (rep--) lengths[index++] = static_cast<short>(len);
  }
  if (lengths[256] == 0) return false;
  if (!lencode.Build(lengths, nlen)) return false;
  if (!distcode.Build(lengths + nlen, ndist)) return false;
  return InflateCodes(br, out, lencode, distcode);
}

// ---------------------------------------------------------------------------
// PNG
// ---------------------------------------------------------------------------

bool LoadPng(const std::vector<unsigned char>& d, Image& img, std::string& error) {
  static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  if (d.size() < 8 || std::memcmp(d.data(), sig, 8) != 0) { error = "not a PNG"; return false; }
  auto be32 = [&](size_t at) { return (static_cast<unsigned>(d[at]) << 24) | (d[at + 1] << 16) | (d[at + 2] << 8) | d[at + 3]; };
  size_t pos = 8;
  int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
  std::vector<unsigned char> idat, palette, trns;
  bool have_ihdr = false;
  while (pos + 8 <= d.size()) {
    const unsigned len = be32(pos);
    const char* type = reinterpret_cast<const char*>(&d[pos + 4]);
    if (pos + 12 + len > d.size()) { error = "truncated PNG"; return false; }
    const unsigned char* body = &d[pos + 8];
    if (std::strncmp(type, "IHDR", 4) == 0 && len >= 13) {
      w = static_cast<int>(be32(pos + 8)); h = static_cast<int>(be32(pos + 12));
      depth = body[8]; ctype = body[9]; interlace = body[12];
      have_ihdr = true;
    } else if (std::strncmp(type, "PLTE", 4) == 0) palette.assign(body, body + len);
    else if (std::strncmp(type, "tRNS", 4) == 0) trns.assign(body, body + len);
    else if (std::strncmp(type, "IDAT", 4) == 0) idat.insert(idat.end(), body, body + len);
    else if (std::strncmp(type, "IEND", 4) == 0) break;
    pos += 12 + len;
  }
  if (!have_ihdr || w <= 0 || h <= 0) { error = "PNG without IHDR"; return false; }
  if (interlace != 0) { error = "interlaced PNG files are not supported"; return false; }
  int channels = 0;
  switch (ctype) {
    case 0: channels = 1; break; case 2: channels = 3; break; case 3: channels = 1; break;
    case 4: channels = 2; break; case 6: channels = 4; break;
    default: error = "unsupported PNG colour type"; return false;
  }
  if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) { error = "unsupported PNG bit depth"; return false; }
  if ((depth < 8 && ctype != 0 && ctype != 3) || (depth == 16 && ctype == 3)) { error = "unsupported PNG depth/colour combination"; return false; }
  std::vector<unsigned char> raw;
  if (!ZlibInflate(idat.data(), idat.size(), raw, error)) return false;
  const size_t bits_per_pixel = static_cast<size_t>(channels) * depth;
  const size_t stride = (static_cast<size_t>(w) * bits_per_pixel + 7) / 8;
  const size_t bpp = std::max<size_t>(1, bits_per_pixel / 8);
  if (raw.size() < (stride + 1) * static_cast<size_t>(h)) { error = "PNG image data too short"; return false; }
  // Unfilter in place (scanlines are prefixed by their filter type).
  std::vector<unsigned char> prev(stride, 0), cur(stride);
  img.width = w; img.height = h;
  img.rgba.assign(static_cast<size_t>(w) * h * 4, 255);
  for (int y = 0; y < h; ++y) {
    const unsigned char filter = raw[static_cast<size_t>(y) * (stride + 1)];
    const unsigned char* src = &raw[static_cast<size_t>(y) * (stride + 1) + 1];
    for (size_t i = 0; i < stride; ++i) {
      const int a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      int v = src[i];
      switch (filter) {
        case 0: break;
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) / 2; break;
        case 4: { const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c); v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
        default: error = "bad PNG filter type"; return false;
      }
      cur[i] = static_cast<unsigned char>(v);
    }
    // Expand the scanline to RGBA.
    for (int x = 0; x < w; ++x) {
      unsigned char* px = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
      int s[4] = {0, 0, 0, 255};
      if (depth == 8) for (int k = 0; k < channels; ++k) s[k] = cur[static_cast<size_t>(x) * channels + k];
      else if (depth == 16) for (int k = 0; k < channels; ++k) s[k] = cur[(static_cast<size_t>(x) * channels + k) * 2];
      else {
        const size_t bit = static_cast<size_t>(x) * depth;
        const int v = (cur[bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1);
        s[0] = ctype == 3 ? v : v * 255 / ((1 << depth) - 1);
      }
      if (ctype == 0) { px[0] = px[1] = px[2] = static_cast<unsigned char>(s[0]); if (trns.size() >= 2 && s[0] == ((trns[0] << 8) | trns[1]) && depth == 8) px[3] = 0; }
      else if (ctype == 2) { px[0] = static_cast<unsigned char>(s[0]); px[1] = static_cast<unsigned char>(s[1]); px[2] = static_cast<unsigned char>(s[2]); }
      else if (ctype == 3) {
        const size_t pi = static_cast<size_t>(s[0]) * 3;
        if (pi + 2 < palette.size()) { px[0] = palette[pi]; px[1] = palette[pi + 1]; px[2] = palette[pi + 2]; }
        if (static_cast<size_t>(s[0]) < trns.size()) px[3] = trns[static_cast<size_t>(s[0])];
      }
      else if (ctype == 4) { px[0] = px[1] = px[2] = static_cast<unsigned char>(s[0]); px[3] = static_cast<unsigned char>(s[1]); }
      else { px[0] = static_cast<unsigned char>(s[0]); px[1] = static_cast<unsigned char>(s[1]); px[2] = static_cast<unsigned char>(s[2]); px[3] = static_cast<unsigned char>(s[3]); }
    }
    std::swap(prev, cur);
  }
  return true;
}

}  // namespace

bool ZlibInflate(const unsigned char* data, size_t size, std::vector<unsigned char>& out, std::string& error) {
  if (size < 2 || (data[0] & 0x0f) != 8 || ((data[0] << 8) | data[1]) % 31 != 0) { error = "bad zlib header"; return false; }
  if (data[1] & 0x20) { error = "zlib preset dictionaries are not supported"; return false; }
  BitReader br{data + 2, size - 2};
  int last = 0;
  do {
    last = br.Bits(1);
    const int type = br.Bits(2);
    bool ok = false;
    if (type == 0) ok = InflateStored(br, out);
    else if (type == 1) ok = InflateFixed(br, out);
    else if (type == 2) ok = InflateDynamic(br, out);
    if (!ok || br.overrun) { error = "corrupt deflate stream"; return false; }
  } while (!last);
  return true;
}

bool LoadImageFile(const std::string& path, Image& out, std::string& error) {
  std::vector<unsigned char> data;
  if (!ReadFile(path, data)) { error = "Cannot read " + path; return false; }
  if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P') return LoadPng(data, out, error);
  if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M') return LoadBmp(data, out, error);
  if (data.size() >= 2 && data[0] == 'P') return LoadPpm(data, out, error);
  error = "Unsupported image format: " + LowerExt(path) + " (BMP, PPM/PGM and PNG are supported)";
  return false;
}

bool SaveImageRGB(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb, std::string& error) {
  if (w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>(w) * h * 3) { error = "Nothing to save"; return false; }
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) { error = "Cannot write " + path; return false; }
  if (LowerExt(path) == ".ppm") {
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, static_cast<size_t>(w) * h * 3, f);
    std::fclose(f);
    return true;
  }
  const int row = (w * 3 + 3) & ~3;
  const unsigned data_size = static_cast<unsigned>(row) * static_cast<unsigned>(h);
  unsigned char hdr[54] = {'B', 'M'};
  auto put32 = [&](int at, unsigned v) { for (int i = 0; i < 4; ++i) hdr[at + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff); };
  auto put16 = [&](int at, unsigned v) { hdr[at] = static_cast<unsigned char>(v & 0xff); hdr[at + 1] = static_cast<unsigned char>((v >> 8) & 0xff); };
  put32(2, 54 + data_size); put32(10, 54); put32(14, 40); put32(18, static_cast<unsigned>(w)); put32(22, static_cast<unsigned>(h));
  put16(26, 1); put16(28, 24); put32(34, data_size);
  std::fwrite(hdr, 1, 54, f);
  std::vector<unsigned char> line(static_cast<size_t>(row), 0);
  for (int y = h - 1; y >= 0; --y) {  // BMP rows are bottom-up
    for (int x = 0; x < w; ++x) {
      const unsigned char* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
      line[static_cast<size_t>(x) * 3] = p[2]; line[static_cast<size_t>(x) * 3 + 1] = p[1]; line[static_cast<size_t>(x) * 3 + 2] = p[0];
    }
    std::fwrite(line.data(), 1, line.size(), f);
  }
  std::fclose(f);
  return true;
}

}  // namespace dino8::app
