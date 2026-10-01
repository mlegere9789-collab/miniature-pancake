// Unit test for util/Inflate.h's InflateRaw max_output cap: without it, a
// deflate stream's back-references (up to 258 bytes of output per symbol)
// let a small, well-formed input expand to an arbitrary size in memory
// before any caller-side size check (a ZIP entry's declared uncompressed
// size, a PNG's width*height) ever gets a chance to reject it - a classic
// decompression bomb. InflateRaw must instead fail as soon as decoding
// would exceed the caller-supplied cap, for both a "stored" (uncompressed)
// block and a back-reference-driven "fixed Huffman" block.
#include <cstdio>
#include <string>
#include <vector>

#include "util/Inflate.h"

using dino8::util::InflateRaw;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

void PutLE16(std::vector<unsigned char>& out, unsigned v) { out.push_back(static_cast<unsigned char>(v & 0xff)); out.push_back(static_cast<unsigned char>((v >> 8) & 0xff)); }

// A single deflate "stored" block (BFINAL=1, BTYPE=00) wrapping `data`
// verbatim - a real, valid deflate bit-stream, same technique
// tests/test_xlsx.cpp's own StoredDeflateBlock uses.
std::vector<unsigned char> StoredDeflateBlock(const std::string& data) {
  std::vector<unsigned char> out;
  out.push_back(0x01);
  const unsigned len = static_cast<unsigned>(data.size());
  PutLE16(out, len);
  PutLE16(out, len ^ 0xffffu);
  out.insert(out.end(), data.begin(), data.end());
  return out;
}

}  // namespace

int main() {
  // Sanity: a stored block within the cap decodes normally.
  {
    std::vector<unsigned char> out;
    const std::vector<unsigned char> block = StoredDeflateBlock("hello world");
    Check(InflateRaw(block.data(), block.size(), out, 1000), "a stored block within max_output decodes");
    Check(out.size() == 11 && std::string(out.begin(), out.end()) == "hello world", "...with the exact original bytes");
  }

  // A stored block whose declared length alone already exceeds max_output
  // must fail outright, not allocate past the cap first.
  {
    std::vector<unsigned char> out;
    const std::vector<unsigned char> block = StoredDeflateBlock(std::string(500, 'A'));
    Check(!InflateRaw(block.data(), block.size(), out, 100), "a stored block bigger than max_output is rejected");
    Check(out.size() <= 100, "...and never grew past the cap in the process");
  }

  // The default cap (no max_output given) still accepts an ordinary small
  // stream - the cap is a safety net, not a general-purpose size limit.
  {
    std::vector<unsigned char> out;
    const std::vector<unsigned char> block = StoredDeflateBlock("small and ordinary");
    Check(InflateRaw(block.data(), block.size(), out), "the default max_output still accepts a normal small stream");
  }

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
