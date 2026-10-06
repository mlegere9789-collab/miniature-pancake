// Regression test for a decompression-bomb gap in LoadPng (src/render/
// ImageIO.cpp): until fixed, LoadPng() called util::InflateRaw with no
// max_output argument at all, so it decompressed up to the generic
// util::kDefaultMaxInflateOutput ceiling (256 MiB) regardless of what the
// PNG's own IHDR width/height/depth/colour type actually needed - a file
// declaring e.g. 1x1 pixels (2 raw bytes needed) could still force a
// multi-hundred-megabyte allocation and a real CPU cost via ordinary
// DEFLATE back-references, entirely before the "is raw.size() too SHORT"
// check a few lines later ever got a chance to reject anything (that
// check never catches too MUCH decompressed data). This mirrors the exact
// untrusted-declared-size class drafting/Xlsx.cpp's own zip-entry reader
// already guards against (see FindZipEntry's own `cap` computation) - see
// util/Inflate.h's kDefaultMaxInflateOutput doc comment, which already
// names "a PNG's width*height" as exactly this kind of caller-known bound.
//
// Builds a minimal, spec-valid PNG by hand: a 1x1, 8-bit grayscale IHDR,
// and a hand-encoded RFC 1951 fixed-Huffman DEFLATE stream (two literal
// bytes, then many (length=258, distance=1) back-references) that expands
// to several megabytes - without needing any deflate ENCODER from this
// codebase (EncodePng only ever writes uncompressed "stored" blocks, which
// can't amplify at all).
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "render/ImageIO.h"
#include "util/Inflate.h"

using dino8::app::Image;
using dino8::app::LoadImageFile;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

bool WriteFile(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
  FILE* f = std::fopen(path.string().c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  std::fclose(f);
  return ok;
}

// Same LSB-first-per-byte bit packing BitReader (util/Inflate.cpp) reads,
// plus a Huffman-code writer (MSB-first per RFC 1951's own tabulation of
// the fixed code tables) - the minimum needed to hand-assemble one valid
// fixed-Huffman DEFLATE block.
struct BitWriter {
  std::vector<unsigned char> bytes;
  unsigned cur = 0;
  int nbits = 0;
  void PutBit(int b) {
    cur |= (static_cast<unsigned>(b & 1) << nbits);
    if (++nbits == 8) { bytes.push_back(static_cast<unsigned char>(cur)); cur = 0; nbits = 0; }
  }
  void PutBitsLSBFirst(unsigned v, int n) { for (int i = 0; i < n; ++i) PutBit(static_cast<int>((v >> i) & 1u)); }
  void PutHuffman(unsigned code, int length) { for (int i = length - 1; i >= 0; --i) PutBit(static_cast<int>((code >> i) & 1u)); }
  void Flush() { if (nbits > 0) { bytes.push_back(static_cast<unsigned char>(cur)); cur = 0; nbits = 0; } }
};

uint32_t Crc32Local(const unsigned char* data, size_t n) { return dino8::util::Crc32(data, n); }

void PutBe32(std::vector<unsigned char>& out, uint32_t v) {
  out.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
  out.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
  out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
  out.push_back(static_cast<unsigned char>(v & 0xFF));
}

// Builds a 1x1, 8-bit grayscale PNG (2 raw bytes genuinely needed: one
// filter-type byte plus one sample) whose IDAT stream decompresses to
// roughly `target_bytes` instead, via repeated (length 258, distance 1)
// back-references after two literal bytes.
std::vector<unsigned char> BuildOneByOnePngBomb(long target_bytes, long& out_decompressed_size) {
  BitWriter bw;
  bw.PutBitsLSBFirst(1, 1);  // BFINAL = 1
  bw.PutBitsLSBFirst(1, 2);  // BTYPE = 01 (fixed Huffman)
  bw.PutHuffman(0x30, 8);    // literal 0 (PNG filter type "None")
  bw.PutHuffman(0x30, 8);    // literal 0 (the lone gray sample)
  long produced = 2;
  while (produced + 258 <= target_bytes) {
    bw.PutHuffman(0xC5, 8);  // length symbol 285 -> length 258, 0 extra bits
    bw.PutHuffman(0x00, 5);  // distance symbol 0 -> distance 1, 0 extra bits
    produced += 258;
  }
  bw.PutHuffman(0x00, 7);  // end-of-block (symbol 256)
  bw.Flush();
  out_decompressed_size = produced;

  std::vector<unsigned char> idat;
  idat.push_back(0x78);
  idat.push_back(0x01);
  idat.insert(idat.end(), bw.bytes.begin(), bw.bytes.end());
  // A correctly-computed Adler-32 trailer (of `produced` zero bytes) -
  // not actually checked by this codebase's inflate path, but kept for a
  // genuinely well-formed zlib stream.
  uint32_t a = 1, b = 0;
  const uint32_t kMod = 65521;
  for (long i = 0; i < produced; ++i) { a = (a + 0) % kMod; b = (b + a) % kMod; }
  const uint32_t adler = (b << 16) | a;
  idat.push_back(static_cast<unsigned char>((adler >> 24) & 0xFF));
  idat.push_back(static_cast<unsigned char>((adler >> 16) & 0xFF));
  idat.push_back(static_cast<unsigned char>((adler >> 8) & 0xFF));
  idat.push_back(static_cast<unsigned char>(adler & 0xFF));

  std::vector<unsigned char> png;
  static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  png.insert(png.end(), sig, sig + 8);
  auto emit_chunk = [&](const char type[4], const std::vector<unsigned char>& body) {
    PutBe32(png, static_cast<uint32_t>(body.size()));
    std::vector<unsigned char> crc_input(type, type + 4);
    crc_input.insert(crc_input.end(), body.begin(), body.end());
    png.insert(png.end(), crc_input.begin(), crc_input.begin() + 4);
    png.insert(png.end(), body.begin(), body.end());
    PutBe32(png, Crc32Local(crc_input.data(), crc_input.size()));
  };
  std::vector<unsigned char> ihdr;
  PutBe32(ihdr, 1);  // width
  PutBe32(ihdr, 1);  // height
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(0);  // colour type 0 = grayscale
  ihdr.push_back(0);  // compression method
  ihdr.push_back(0);  // filter method
  ihdr.push_back(0);  // interlace method
  emit_chunk("IHDR", ihdr);
  emit_chunk("IDAT", idat);
  emit_chunk("IEND", {});
  return png;
}
}  // namespace

int main() {
  const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "dino8_test_png_inflate_bomb";
  std::filesystem::create_directories(tmp);

  // ---- The bomb itself: must be rejected, not decompressed -------------
  {
    long decompressed_size = 0;
    const std::vector<unsigned char> png = BuildOneByOnePngBomb(5'000'000, decompressed_size);
    Check(decompressed_size > 4'000'000, "bomb setup: the crafted stream really does decode to several MB");
    const std::filesystem::path path = tmp / "bomb_1x1.png";
    Check(WriteFile(path, png), "bomb setup: wrote the crafted PNG to disk");

    Image img;
    std::string error;
    const bool ok = LoadImageFile(path.string(), img, error);
    Check(!ok, "LoadImageFile rejects a 1x1 PNG whose IDAT decompresses to several MB instead of silently "
               "decompressing the whole thing");
  }

  // ---- A much larger bomb: must fail fast (milliseconds), not spend a --
  // ---- real CPU/memory budget on a file that claims to be 1x1 ----------
  {
    long decompressed_size = 0;
    const std::vector<unsigned char> png = BuildOneByOnePngBomb(100'000'000, decompressed_size);
    const std::filesystem::path path = tmp / "bomb_1x1_100mb.png";
    Check(WriteFile(path, png), "100MB-bomb setup: wrote the crafted PNG to disk");

    Image img;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = LoadImageFile(path.string(), img, error);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    Check(!ok, "LoadImageFile rejects a 1x1 PNG whose IDAT decompresses to ~100MB");
    Check(ms < 200.0, "LoadImageFile rejects the ~100MB bomb in well under 200ms (it is capped near the image's "
                       "own tiny declared size, not left to actually decompress ~100MB first)");
  }

  // ---- A genuine, small PNG must still load correctly (no regression) --
  {
    const int w = 5, h = 3;
    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        unsigned char* px = &rgb[(static_cast<size_t>(y) * w + x) * 3];
        px[0] = static_cast<unsigned char>(x * 40);
        px[1] = static_cast<unsigned char>(y * 60);
        px[2] = static_cast<unsigned char>(100);
      }
    }
    std::vector<unsigned char> png;
    std::string error;
    Check(dino8::app::EncodePng(w, h, rgb, png, error), "legitimate-PNG setup: EncodePng succeeds");
    const std::filesystem::path path = tmp / "legit.png";
    Check(WriteFile(path, png), "legitimate-PNG setup: wrote the encoded PNG to disk");

    Image img;
    const bool ok = LoadImageFile(path.string(), img, error);
    Check(ok, ("LoadImageFile still loads a genuine, correctly-sized PNG: " + error).c_str());
    Check(img.width == w && img.height == h, "LoadImageFile reports the correct dimensions for the genuine PNG");
    bool pixels_match = ok && img.width == w && img.height == h;
    for (int y = 0; pixels_match && y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const unsigned char* src = &rgb[(static_cast<size_t>(y) * w + x) * 3];
        const unsigned char* dst = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
        if (src[0] != dst[0] || src[1] != dst[1] || src[2] != dst[2]) pixels_match = false;
      }
    }
    Check(pixels_match, "LoadImageFile recovers the genuine PNG's exact pixel values");
  }

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
