#include "render/ImageIO.h"

#include "util/Inflate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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
  const size_t bits_per_pixel = static_cast<size_t>(channels) * depth;
  const size_t stride = (static_cast<size_t>(w) * bits_per_pixel + 7) / 8;
  const size_t bpp = std::max<size_t>(1, bits_per_pixel / 8);
  // Cap decompression at exactly what this image's own declared
  // width/height/depth/colour type need (clamped to the generic ceiling
  // in case even that legitimate size is absurd) - not the generic
  // kDefaultMaxInflateOutput default alone. Without this, a file
  // declaring tiny dimensions (e.g. 1x1) can carry an IDAT stream that
  // decompresses, via ordinary DEFLATE back-references, to the full
  // 256 MiB default ceiling regardless of what the image actually needs
  // - confirmed directly: a ~31 KB crafted 1x1 PNG forced a ~5 MB
  // allocation, and a ~1.6 MB one forced ~260 MB and over a second of
  // CPU, before this file's own "too short" check below ever got a
  // chance to reject anything (that check only catches too LITTLE
  // decompressed data, never too much). See
  // Inflate.h's own kDefaultMaxInflateOutput doc comment, which already
  // names "a PNG's width*height" as exactly the kind of caller-known
  // bound that should be used instead of the generic ceiling - this is
  // that fix, applied here.
  const size_t expected_raw_size = (stride + 1) * static_cast<size_t>(h);
  const size_t cap = std::min(expected_raw_size, dino8::util::kDefaultMaxInflateOutput);
  std::vector<unsigned char> raw;
  if (!ZlibInflate(idat.data(), idat.size(), raw, error, cap)) return false;
  if (raw.size() < expected_raw_size) { error = "PNG image data too short"; return false; }
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

// ---------------------------------------------------------------------------
// PNG encoding (write side): CRC-32 and Adler-32 checksums, a "stored"
// (uncompressed) deflate block writer, and the IHDR/IDAT/IEND chunk layout.
// Real, spec-conformant output (RFC 2083 / RFC 1950 / RFC 1951) - just not
// compressed, since correctness (a byte-exact, standards-conformant PNG any
// reader can decode) matters far more here than file size for a clipboard
// image. Crc32 itself now lives in util/Inflate.h/.cpp (shared with
// drafting/Xlsx.cpp's ZIP reader/writer, which needs the identical
// checksum) - `using dino8::util::Crc32` below keeps every call site in
// this file unchanged.
// ---------------------------------------------------------------------------

using dino8::util::Crc32;

uint32_t Adler32(const unsigned char* data, size_t n) {
  uint32_t a = 1, b = 0;
  const uint32_t MOD = 65521;
  for (size_t i = 0; i < n; ++i) {
    a = (a + data[i]) % MOD;
    b = (b + a) % MOD;
  }
  return (b << 16) | a;
}

// Wraps `raw` in a minimal zlib stream (RFC 1950 header, RFC 1951 "stored"
// (type 00) deflate blocks - each literally the input bytes with a 5-byte
// block header, valid and fully decodable even though it does not actually
// compress anything, RFC 1951 section 3.2.4 - and the trailing Adler-32).
void ZlibDeflateStored(const std::vector<unsigned char>& raw, std::vector<unsigned char>& out) {
  out.clear();
  out.push_back(0x78);  // CMF: CM=8 (deflate), CINFO=7 (32K window)
  out.push_back(0x01);  // FLG: FCHECK makes (CMF<<8|FLG) a multiple of 31; FLEVEL=0 (fastest, honest for stored blocks)
  size_t pos = 0;
  const size_t n = raw.size();
  do {
    const size_t chunk = std::min<size_t>(65535, n - pos);
    const bool final_block = (pos + chunk >= n);
    out.push_back(final_block ? 0x01 : 0x00);  // BFINAL | BTYPE(00)<<1, byte-aligned (3 header bits + zero padding)
    const unsigned len = static_cast<unsigned>(chunk);
    const unsigned nlen = (~len) & 0xFFFFu;
    out.push_back(static_cast<unsigned char>(len & 0xFF));
    out.push_back(static_cast<unsigned char>((len >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>(nlen & 0xFF));
    out.push_back(static_cast<unsigned char>((nlen >> 8) & 0xFF));
    out.insert(out.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + chunk));
    pos += chunk;
  } while (pos < n);
  const uint32_t adler = Adler32(raw.data(), raw.size());
  out.push_back(static_cast<unsigned char>((adler >> 24) & 0xFF));
  out.push_back(static_cast<unsigned char>((adler >> 16) & 0xFF));
  out.push_back(static_cast<unsigned char>((adler >> 8) & 0xFF));
  out.push_back(static_cast<unsigned char>(adler & 0xFF));
}

void PutBe32(std::vector<unsigned char>& out, uint32_t v) {
  out.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
  out.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
  out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
  out.push_back(static_cast<unsigned char>(v & 0xFF));
}

// ---------------------------------------------------------------------------
// Radiance HDR / RGBE (.hdr)
// ---------------------------------------------------------------------------

// Ward's shared-exponent encode: the largest of r,g,b picks a common
// power-of-two exponent, and all three channels are quantized to 8 bits of
// mantissa against it. Non-negative, non-finite-safe (NaN/Inf clamp to 0
// via the `m < 1e-32f` branch, since frexpf on them is undefined-ish and
// callers should never feed a renderer NaN through here anyway).
void EncodeRgbe(float r, float g, float b, unsigned char out[4]) {
  float m = std::max(r, std::max(g, b));
  if (!(m > 1e-32f) || !std::isfinite(m)) { out[0] = out[1] = out[2] = out[3] = 0; return; }
  int e = 0;
  const float mantissa = std::frexp(m, &e);
  const float scale = mantissa * 256.0f / m;
  auto q = [&](float c) { return static_cast<unsigned char>(std::clamp(c * scale, 0.0f, 255.0f)); };
  out[0] = q(r); out[1] = q(g); out[2] = q(b);
  out[3] = static_cast<unsigned char>(std::clamp(e + 128, 0, 255));
}

void DecodeRgbe(const unsigned char in[4], float& r, float& g, float& b) {
  if (in[3] == 0) { r = g = b = 0.0f; return; }
  const float f = std::ldexp(1.0f, static_cast<int>(in[3]) - (128 + 8));
  r = (in[0] + 0.5f) * f; g = (in[1] + 0.5f) * f; b = (in[2] + 0.5f) * f;
}

// Decodes one new-style RLE scanline's four component planes (R, G, B, E,
// each independently run-length coded) into `scan` (w*4 bytes, RGBE
// interleaved). Per Radiance's spec: a byte > 128 starts a run of
// (byte - 128) copies of the next byte; a byte in [1,128] is a literal
// count of that many raw bytes.
bool DecodeRleScanline(const unsigned char* d, size_t n, size_t& pos, int w, std::vector<unsigned char>& scan) {
  scan.assign(static_cast<size_t>(w) * 4, 0);
  for (int chan = 0; chan < 4; ++chan) {
    int x = 0;
    while (x < w) {
      if (pos >= n) return false;
      const unsigned char count = d[pos++];
      if (count > 128) {
        const int run = count - 128;
        if (pos >= n || x + run > w) return false;
        const unsigned char value = d[pos++];
        for (int i = 0; i < run; ++i) scan[static_cast<size_t>(x++) * 4 + chan] = value;
      } else {
        const int lit = count;
        if (lit == 0 || pos + static_cast<size_t>(lit) > n || x + lit > w) return false;
        for (int i = 0; i < lit; ++i) scan[static_cast<size_t>(x++) * 4 + chan] = d[pos++];
      }
    }
  }
  return true;
}

}  // namespace

bool EncodePng(int width, int height, const std::vector<unsigned char>& rgb, std::vector<unsigned char>& out,
                std::string& error) {
  if (width <= 0 || height <= 0 || rgb.size() < static_cast<size_t>(width) * height * 3) {
    error = "Nothing to encode";
    return false;
  }
  // Raw scanlines: each row is a filter-type byte (0 = None) followed by
  // width*3 RGB bytes, top row first (PNG scans top-to-bottom).
  const size_t row_bytes = static_cast<size_t>(width) * 3;
  std::vector<unsigned char> raw(static_cast<size_t>(height) * (row_bytes + 1));
  for (int y = 0; y < height; ++y) {
    unsigned char* dst = &raw[static_cast<size_t>(y) * (row_bytes + 1)];
    dst[0] = 0;  // filter type None
    std::memcpy(dst + 1, &rgb[static_cast<size_t>(y) * row_bytes], row_bytes);
  }
  std::vector<unsigned char> zlib_stream;
  ZlibDeflateStored(raw, zlib_stream);

  out.clear();
  static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  out.insert(out.end(), kSig, kSig + 8);

  auto emit_chunk = [&](const char type[4], const std::vector<unsigned char>& body) {
    PutBe32(out, static_cast<uint32_t>(body.size()));
    std::vector<unsigned char> crc_input(type, type + 4);
    crc_input.insert(crc_input.end(), body.begin(), body.end());
    out.insert(out.end(), crc_input.begin(), crc_input.begin() + 4);
    out.insert(out.end(), body.begin(), body.end());
    PutBe32(out, Crc32(crc_input.data(), crc_input.size()));
  };

  std::vector<unsigned char> ihdr;
  PutBe32(ihdr, static_cast<uint32_t>(width));
  PutBe32(ihdr, static_cast<uint32_t>(height));
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // colour type 2 = truecolor RGB
  ihdr.push_back(0);  // compression method (only value defined by the spec)
  ihdr.push_back(0);  // filter method (only value defined by the spec)
  ihdr.push_back(0);  // interlace method: none
  emit_chunk("IHDR", ihdr);
  emit_chunk("IDAT", zlib_stream);
  emit_chunk("IEND", {});
  return true;
}

bool ZlibInflate(const unsigned char* data, size_t size, std::vector<unsigned char>& out, std::string& error,
                  size_t max_output) {
  if (size < 2 || (data[0] & 0x0f) != 8 || ((data[0] << 8) | data[1]) % 31 != 0) { error = "bad zlib header"; return false; }
  if (data[1] & 0x20) { error = "zlib preset dictionaries are not supported"; return false; }
  if (!dino8::util::InflateRaw(data + 2, size - 2, out, max_output)) { error = "corrupt deflate stream"; return false; }
  return true;
}

// Reinhard tone-map (c/(1+c)) plus gamma 2.2, so an 8-bit consumer (texture
// atlases, thumbnails) gets a sane preview of an HDR image's LDR range
// instead of every above-1.0 highlight clipping straight to white.
void TonemapHdrToRgba(const ImageHdr& hdr, Image& out) {
  out.width = hdr.width; out.height = hdr.height;
  out.rgba.assign(static_cast<size_t>(hdr.width) * hdr.height * 4, 255);
  for (size_t i = 0; i < static_cast<size_t>(hdr.width) * hdr.height; ++i) {
    for (int c = 0; c < 3; ++c) {
      const float lin = std::max(0.0f, hdr.rgb[i * 3 + static_cast<size_t>(c)]);
      const float mapped = std::pow(lin / (1.0f + lin), 1.0f / 2.2f);
      out.rgba[i * 4 + static_cast<size_t>(c)] = static_cast<unsigned char>(std::clamp(mapped * 255.0f + 0.5f, 0.0f, 255.0f));
    }
  }
}

bool LoadImageFile(const std::string& path, Image& out, std::string& error) {
  std::vector<unsigned char> data;
  if (!ReadFile(path, data)) { error = "Cannot read " + path; return false; }
  if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P') return LoadPng(data, out, error);
  if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M') return LoadBmp(data, out, error);
  if (data.size() >= 2 && data[0] == '#' && data[1] == '?') {
    ImageHdr hdr;
    if (!LoadImageHdr(path, hdr, error)) return false;
    TonemapHdrToRgba(hdr, out);
    return true;
  }
  if (data.size() >= 4 && data[0] == 0x76 && data[1] == 0x2f && data[2] == 0x31 && data[3] == 0x01) {
    ImageHdr hdr;
    if (!LoadImageExr(path, hdr, error)) return false;
    TonemapHdrToRgba(hdr, out);
    return true;
  }
  if (data.size() >= 2 && data[0] == 'P') return LoadPpm(data, out, error);
  error = "Unsupported image format: " + LowerExt(path) + " (BMP, PPM/PGM, PNG, HDR and EXR are supported)";
  return false;
}

bool LoadImageHdr(const std::string& path, ImageHdr& out, std::string& error) {
  std::vector<unsigned char> data;
  if (!ReadFile(path, data)) { error = "Cannot read " + path; return false; }
  if (data.size() < 2 || data[0] != '#' || data[1] != '?') { error = "Not a Radiance HDR file"; return false; }
  size_t pos = 0;
  auto read_line = [&](std::string& line) {
    line.clear();
    while (pos < data.size() && data[pos] != '\n') line.push_back(static_cast<char>(data[pos++]));
    if (pos < data.size()) ++pos;  // skip '\n'
    return !line.empty() || pos <= data.size();
  };
  std::string line;
  read_line(line);  // "#?RADIANCE" / "#?RGBE" magic, already checked above
  bool got_format = false;
  for (;;) {
    if (pos >= data.size()) { error = "Truncated HDR header"; return false; }
    if (!read_line(line)) { error = "Truncated HDR header"; return false; }
    if (line.empty()) break;  // blank line ends the header
    if (line.rfind("FORMAT=", 0) == 0) got_format = true;
  }
  (void)got_format;  // informational only — every real-world writer emits 32-bit_rle_rgbe; nothing else is defined
  if (pos >= data.size() || !read_line(line)) { error = "Missing HDR resolution line"; return false; }
  int h = 0, w = 0;
  if (std::sscanf(line.c_str(), "-Y %d +X %d", &h, &w) != 2) {
    error = "Unsupported HDR orientation (only top-down -Y H +X W is supported): " + line;
    return false;
  }
  if (w <= 0 || h <= 0 || static_cast<int64_t>(w) * h > (1 << 28)) { error = "Invalid HDR resolution"; return false; }
  out.width = w; out.height = h;
  out.rgb.assign(static_cast<size_t>(w) * h * 3, 0.0f);
  std::vector<unsigned char> scan;
  for (int y = 0; y < h; ++y) {
    // New-style RLE marker: 2,2,(w>>8)&0xff,w&0xff, only used for scanlines
    // 8..0x7fff pixels wide (older/odd-width files fall back to flat).
    const bool can_rle = w >= 8 && w < 0x7fff;
    bool is_rle = false;
    if (can_rle && pos + 4 <= data.size() && data[pos] == 2 && data[pos + 1] == 2 &&
        (((data[pos + 2] << 8) | data[pos + 3]) == w)) {
      is_rle = true;
      pos += 4;
    }
    if (is_rle) {
      if (!DecodeRleScanline(data.data(), data.size(), pos, w, scan)) { error = "Corrupt HDR RLE scanline"; return false; }
      for (int x = 0; x < w; ++x) {
        float r, g, b;
        DecodeRgbe(&scan[static_cast<size_t>(x) * 4], r, g, b);
        const size_t o = (static_cast<size_t>(y) * w + x) * 3;
        out.rgb[o] = r; out.rgb[o + 1] = g; out.rgb[o + 2] = b;
      }
    } else {
      // Flat (uncompressed) scanline: w*4 raw RGBE bytes, possibly with the
      // 4 bytes already consumed above as an ordinary (non-marker) pixel.
      const size_t need = static_cast<size_t>(w) * 4;
      if (pos + need > data.size()) { error = "Truncated HDR scanline data"; return false; }
      for (int x = 0; x < w; ++x) {
        float r, g, b;
        DecodeRgbe(&data[pos + static_cast<size_t>(x) * 4], r, g, b);
        const size_t o = (static_cast<size_t>(y) * w + x) * 3;
        out.rgb[o] = r; out.rgb[o + 1] = g; out.rgb[o + 2] = b;
      }
      pos += need;
    }
  }
  return true;
}

bool SaveImageHdr(const std::string& path, int w, int h, const std::vector<float>& rgb, std::string& error) {
  if (w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>(w) * h * 3) { error = "Nothing to save"; return false; }
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) { error = "Cannot write " + path; return false; }
  std::fprintf(f, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n", h, w);
  std::vector<unsigned char> row(static_cast<size_t>(w) * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const size_t i = (static_cast<size_t>(y) * w + x) * 3;
      EncodeRgbe(std::max(0.0f, rgb[i]), std::max(0.0f, rgb[i + 1]), std::max(0.0f, rgb[i + 2]), &row[static_cast<size_t>(x) * 4]);
    }
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
  return true;
}

// ---------------------------------------------------------------------------
// OpenEXR (minimal: single-part scanline, NO_COMPRESSION, HALF/FLOAT R/G/B)
// ---------------------------------------------------------------------------

namespace {

// IEEE 754-2008 binary16 -> binary32. Handles subnormals, inf and NaN; a
// zero-exponent/zero-mantissa half (±0) falls out of the general subnormal
// branch's own `mant == 0` check into a plain signed-zero float, so no
// separate case is needed for it.
float DecodeHalf(uint16_t h) {
  const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
  uint32_t exp = (h >> 10) & 0x1fu;
  uint32_t mant = h & 0x3ffu;
  uint32_t bits;
  if (exp == 0) {
    if (mant == 0) {
      bits = sign;
    } else {
      exp = 1;
      while ((mant & 0x400u) == 0) { mant <<= 1; --exp; }
      mant &= 0x3ffu;
      bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
  } else if (exp == 0x1fu) {
    bits = sign | 0x7f800000u | (mant << 13);
  } else {
    bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
  }
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

struct ExrChannel {
  std::string name;
  int32_t pixel_type = 1;  // 0=UINT, 1=HALF, 2=FLOAT
  int32_t x_sampling = 1, y_sampling = 1;
};

uint32_t ReadLe32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool LoadImageExr(const std::string& path, ImageHdr& out, std::string& error) {
  std::vector<unsigned char> d;
  if (!ReadFile(path, d)) { error = "Cannot read " + path; return false; }
  if (d.size() < 8 || d[0] != 0x76 || d[1] != 0x2f || d[2] != 0x31 || d[3] != 0x01) { error = "Not an EXR file"; return false; }
  const uint32_t version_flags = ReadLe32(&d[4]);
  if ((version_flags & 0xffu) != 2) { error = "Unsupported EXR version"; return false; }
  if (version_flags & (1u << 9)) { error = "Tiled EXR files are not supported"; return false; }
  if (version_flags & (1u << 11)) { error = "Deep-data EXR files are not supported"; return false; }
  if (version_flags & (1u << 12)) { error = "Multi-part EXR files are not supported"; return false; }

  size_t pos = 8;
  auto read_i32 = [&](int32_t& v) {
    if (pos + 4 > d.size()) return false;
    v = static_cast<int32_t>(ReadLe32(&d[pos]));
    pos += 4;
    return true;
  };
  auto read_i64 = [&](int64_t& v) {
    if (pos + 8 > d.size()) return false;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u |= static_cast<uint64_t>(d[pos + static_cast<size_t>(i)]) << (8 * i);
    v = static_cast<int64_t>(u);
    pos += 8;
    return true;
  };
  auto read_cstr = [&](std::string& s) {
    s.clear();
    while (pos < d.size() && d[pos] != 0) s.push_back(static_cast<char>(d[pos++]));
    if (pos >= d.size()) return false;
    ++pos;
    return true;
  };

  std::vector<ExrChannel> channels;
  int32_t data_xmin = 0, data_ymin = 0, data_xmax = -1, data_ymax = -1;
  bool have_data_window = false;
  int compression = -1;
  int line_order = 0;

  for (;;) {
    if (pos >= d.size()) { error = "Truncated EXR header"; return false; }
    if (d[pos] == 0) { ++pos; break; }  // zero-length name ends the attribute list
    std::string name, type;
    int32_t size = 0;
    if (!read_cstr(name) || !read_cstr(type) || !read_i32(size) || size < 0 || pos + static_cast<size_t>(size) > d.size()) {
      error = "Truncated or malformed EXR attribute";
      return false;
    }
    const size_t value_start = pos;
    if (name == "channels" && type == "chlist") {
      size_t p = value_start;
      const size_t end = value_start + static_cast<size_t>(size);
      while (p < end && d[p] != 0) {
        std::string cname;
        while (p < d.size() && d[p] != 0) cname.push_back(static_cast<char>(d[p++]));
        if (p >= d.size() || p + 17 > d.size()) { error = "Truncated EXR channel entry"; return false; }
        ++p;  // skip the channel name's own terminating null
        ExrChannel ch;
        ch.name = cname;
        ch.pixel_type = static_cast<int32_t>(ReadLe32(&d[p])); p += 4;
        p += 4;  // pLinear (1 byte) + reserved (3 bytes)
        ch.x_sampling = static_cast<int32_t>(ReadLe32(&d[p])); p += 4;
        ch.y_sampling = static_cast<int32_t>(ReadLe32(&d[p])); p += 4;
        channels.push_back(std::move(ch));
      }
    } else if (name == "compression" && type == "compression" && size >= 1) {
      compression = d[value_start];
    } else if (name == "dataWindow" && type == "box2i" && size >= 16) {
      data_xmin = static_cast<int32_t>(ReadLe32(&d[value_start]));
      data_ymin = static_cast<int32_t>(ReadLe32(&d[value_start + 4]));
      data_xmax = static_cast<int32_t>(ReadLe32(&d[value_start + 8]));
      data_ymax = static_cast<int32_t>(ReadLe32(&d[value_start + 12]));
      have_data_window = true;
    } else if (name == "lineOrder" && type == "lineOrder" && size >= 1) {
      line_order = d[value_start];
    }
    pos = value_start + static_cast<size_t>(size);
  }

  if (!have_data_window || data_xmax < data_xmin || data_ymax < data_ymin) { error = "Missing or invalid EXR dataWindow"; return false; }
  if (compression != 0) { error = "Only uncompressed (NO_COMPRESSION) EXR files are supported"; return false; }
  if (line_order == 2) { error = "Random-order EXR scanlines are not supported"; return false; }

  int r_idx = -1, g_idx = -1, b_idx = -1;
  for (size_t i = 0; i < channels.size(); ++i) {
    if (channels[i].name == "R") r_idx = static_cast<int>(i);
    else if (channels[i].name == "G") g_idx = static_cast<int>(i);
    else if (channels[i].name == "B") b_idx = static_cast<int>(i);
  }
  if (r_idx < 0 || g_idx < 0 || b_idx < 0) {
    error = "EXR file has no R/G/B channels (only single-layer RGB/RGBA environment images are supported)";
    return false;
  }
  for (const ExrChannel& c : channels) {
    if (c.pixel_type != 1 && c.pixel_type != 2) { error = "Unsupported EXR channel pixel type (only HALF and FLOAT are supported)"; return false; }
    if (c.x_sampling != 1 || c.y_sampling != 1) { error = "Subsampled EXR channels are not supported"; return false; }
  }

  const int w = data_xmax - data_xmin + 1;
  const int h = data_ymax - data_ymin + 1;
  if (w <= 0 || h <= 0 || static_cast<int64_t>(w) * h > (1 << 28)) { error = "Invalid EXR dimensions"; return false; }

  std::vector<int64_t> offsets(static_cast<size_t>(h));
  for (int64_t i = 0; i < h; ++i) {
    if (!read_i64(offsets[static_cast<size_t>(i)])) { error = "Truncated EXR offset table"; return false; }
  }

  out.width = w; out.height = h;
  out.rgb.assign(static_cast<size_t>(w) * h * 3, 0.0f);

  auto channel_bytes = [](const ExrChannel& c) { return c.pixel_type == 1 ? 2 : 4; };

  for (int64_t i = 0; i < h; ++i) {
    const int64_t off = offsets[static_cast<size_t>(i)];
    if (off < 0 || static_cast<size_t>(off) + 8 > d.size()) { error = "Invalid EXR chunk offset"; return false; }
    size_t p = static_cast<size_t>(off);
    const int32_t y = static_cast<int32_t>(ReadLe32(&d[p])); p += 4;
    const int32_t data_size = static_cast<int32_t>(ReadLe32(&d[p])); p += 4;
    if (data_size < 0 || p + static_cast<size_t>(data_size) > d.size()) { error = "Truncated EXR scanline chunk"; return false; }
    if (y < data_ymin || y > data_ymax) { error = "EXR scanline y coordinate out of range"; return false; }
    const int row = y - data_ymin;
    // Channels are stored in header order, each channel's own full `w`-sample
    // row contiguous (planar), not interleaved per pixel - the layout every
    // conformant EXR writer uses for a NO_COMPRESSION scanline chunk.
    std::vector<const unsigned char*> channel_ptr(channels.size());
    size_t cp = p;
    for (size_t ci = 0; ci < channels.size(); ++ci) {
      channel_ptr[ci] = &d[cp];
      cp += static_cast<size_t>(w) * static_cast<size_t>(channel_bytes(channels[ci]));
    }
    if (cp - p != static_cast<size_t>(data_size)) { error = "EXR scanline size does not match its own channel layout"; return false; }
    auto sample_at = [&](int ci, int x) {
      const ExrChannel& c = channels[static_cast<size_t>(ci)];
      const unsigned char* base = channel_ptr[static_cast<size_t>(ci)] + static_cast<size_t>(x) * static_cast<size_t>(channel_bytes(c));
      if (c.pixel_type == 1) return DecodeHalf(static_cast<uint16_t>(base[0] | (base[1] << 8)));
      float f;
      const uint32_t bits = ReadLe32(base);
      std::memcpy(&f, &bits, sizeof(f));
      return f;
    };
    for (int x = 0; x < w; ++x) {
      const size_t o = (static_cast<size_t>(row) * w + x) * 3;
      out.rgb[o] = sample_at(r_idx, x);
      out.rgb[o + 1] = sample_at(g_idx, x);
      out.rgb[o + 2] = sample_at(b_idx, x);
    }
  }
  return true;
}

bool SaveImageExr(const std::string& path, int w, int h, const std::vector<float>& rgb, std::string& error) {
  if (w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>(w) * h * 3) { error = "Nothing to save"; return false; }
  std::vector<unsigned char> buf;
  auto w_bytes = [&](const void* p, size_t n) { const unsigned char* b = static_cast<const unsigned char*>(p); buf.insert(buf.end(), b, b + n); };
  auto w_u8 = [&](unsigned char v) { buf.push_back(v); };
  auto w_i32 = [&](int32_t v) { unsigned char b[4]; for (int i = 0; i < 4; ++i) b[i] = static_cast<unsigned char>((static_cast<uint32_t>(v) >> (8 * i)) & 0xff); w_bytes(b, 4); };
  auto w_i64 = [&](int64_t v) { unsigned char b[8]; const uint64_t u = static_cast<uint64_t>(v); for (int i = 0; i < 8; ++i) b[i] = static_cast<unsigned char>((u >> (8 * i)) & 0xff); w_bytes(b, 8); };
  auto w_float = [&](float v) { unsigned char b[4]; std::memcpy(b, &v, 4); w_bytes(b, 4); };
  auto w_cstr = [&](const char* s) { w_bytes(s, std::strlen(s) + 1); };
  auto w_attr = [&](const char* name, const char* type, int32_t size) { w_cstr(name); w_cstr(type); w_i32(size); };

  w_u8(0x76); w_u8(0x2f); w_u8(0x31); w_u8(0x01);
  w_i32(2);  // version 2, no flags (single-part scanline, non-deep)

  // Channel list (chlist): B, G, R - alphabetical order, the order the
  // spec requires a conformant writer to use (and the order the scanline
  // chunk data below must then also use) - all FLOAT, no subsampling.
  static const char* const kChannelNames[3] = {"B", "G", "R"};
  int32_t chlist_size = 1;  // the list's own terminating null byte
  for (const char* n : kChannelNames) chlist_size += static_cast<int32_t>(std::strlen(n)) + 1 + 16;
  w_attr("channels", "chlist", chlist_size);
  for (const char* n : kChannelNames) {
    w_cstr(n);
    w_i32(2);              // pixelType = FLOAT
    w_u8(0);               // pLinear
    w_u8(0); w_u8(0); w_u8(0);  // reserved
    w_i32(1); w_i32(1);    // x/ySampling
  }
  w_u8(0);  // end of chlist

  w_attr("compression", "compression", 1);
  w_u8(0);  // NO_COMPRESSION

  w_attr("dataWindow", "box2i", 16);
  w_i32(0); w_i32(0); w_i32(w - 1); w_i32(h - 1);

  w_attr("displayWindow", "box2i", 16);
  w_i32(0); w_i32(0); w_i32(w - 1); w_i32(h - 1);

  w_attr("lineOrder", "lineOrder", 1);
  w_u8(0);  // INCREASING_Y

  w_attr("pixelAspectRatio", "float", 4);
  w_float(1.0f);

  w_attr("screenWindowCenter", "v2f", 8);
  w_float(0.0f); w_float(0.0f);

  w_attr("screenWindowWidth", "float", 4);
  w_float(1.0f);

  w_u8(0);  // end of header attribute list

  // Scanline offset table: one entry per row (NO_COMPRESSION means exactly
  // one scanline per chunk), each chunk a fixed `8 + w*4*3` bytes (y +
  // dataSize + 3 planar FLOAT channel rows), so every offset is known
  // analytically from the header's own already-written size.
  const size_t header_end = buf.size();
  const size_t chunk_size = 8 + static_cast<size_t>(w) * 4 * 3;
  const size_t data_start = header_end + static_cast<size_t>(h) * 8;
  for (int y = 0; y < h; ++y) w_i64(static_cast<int64_t>(data_start + static_cast<size_t>(y) * chunk_size));

  for (int y = 0; y < h; ++y) {
    w_i32(y);
    w_i32(static_cast<int32_t>(static_cast<size_t>(w) * 4 * 3));
    // Planar, alphabetical (B, G, R) to match the channel list above.
    for (int c = 2; c >= 0; --c) {  // rgb[] is R,G,B per pixel; write B(2),G(1),R(0)
      for (int x = 0; x < w; ++x) w_float(std::max(0.0f, rgb[(static_cast<size_t>(y) * w + x) * 3 + static_cast<size_t>(c)]));
    }
  }

  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) { error = "Cannot write " + path; return false; }
  std::fwrite(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  return true;
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

bool EncodeBmp(int w, int h, const std::vector<unsigned char>& rgb, std::vector<unsigned char>& out, std::string& error) {
  if (w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>(w) * h * 3) { error = "Nothing to encode"; return false; }
  const int row = (w * 3 + 3) & ~3;
  const unsigned data_size = static_cast<unsigned>(row) * static_cast<unsigned>(h);
  out.assign(54 + data_size, 0);
  out[0] = 'B'; out[1] = 'M';
  auto put32 = [&](int at, unsigned v) { for (int i = 0; i < 4; ++i) out[static_cast<size_t>(at + i)] = static_cast<unsigned char>((v >> (8 * i)) & 0xff); };
  auto put16 = [&](int at, unsigned v) { out[static_cast<size_t>(at)] = static_cast<unsigned char>(v & 0xff); out[static_cast<size_t>(at + 1)] = static_cast<unsigned char>((v >> 8) & 0xff); };
  put32(2, 54 + data_size); put32(10, 54); put32(14, 40); put32(18, static_cast<unsigned>(w)); put32(22, static_cast<unsigned>(h));
  put16(26, 1); put16(28, 24); put32(34, data_size);
  for (int y = h - 1; y >= 0; --y) {  // BMP rows are bottom-up
    unsigned char* line = &out[54 + static_cast<size_t>(h - 1 - y) * row];
    for (int x = 0; x < w; ++x) {
      const unsigned char* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
      line[static_cast<size_t>(x) * 3] = p[2]; line[static_cast<size_t>(x) * 3 + 1] = p[1]; line[static_cast<size_t>(x) * 3 + 2] = p[0];
    }
  }
  return true;
}

}  // namespace dino8::app
