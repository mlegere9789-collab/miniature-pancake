// Unit test for util/Inflate.h's InflateRaw max_output cap: without it, a
// deflate stream's back-references (up to 258 bytes of output per symbol)
// let a small, well-formed input expand to an arbitrary size in memory
// before any caller-side size check (a ZIP entry's declared uncompressed
// size, a PNG's width*height) ever gets a chance to reject it - a classic
// decompression bomb. InflateRaw must instead fail as soon as decoding
// would exceed the caller-supplied cap, for both a "stored" (uncompressed)
// block, where output can only ever match input 1:1, and a back-reference-
// driven "fixed Huffman" block, where a couple of MB of input can expand
// to hundreds of MB of output - the actual mechanism a real-world
// decompression bomb uses, and the one case a stored-block test alone
// can never exercise.
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

// A hand-built "fixed Huffman" (BFINAL=1, BTYPE=01) block encoding one
// literal `byte` followed by `repeats` maximal (258-byte, distance-1)
// back-references - `1 + 258*repeats` bytes of output from a compressed
// stream of about 13 bits per repeat. This is the actual amplification
// mechanism a real zip/PNG decompression bomb exploits (a "stored" block
// can only ever produce output 1:1 with its input); the cap must stop
// *this* path mid-decode, which the stored-block tests above never touch.
std::vector<unsigned char> FixedHuffmanRunBlock(unsigned char byte, size_t repeats) {
  struct BitWriter {
    std::vector<unsigned char> bytes;
    unsigned cur = 0;
    int nbits = 0;
    void Bit(int b) {
      cur |= static_cast<unsigned>(b & 1) << nbits;
      if (++nbits == 8) { bytes.push_back(static_cast<unsigned char>(cur)); cur = 0; nbits = 0; }
    }
    // Data elements other than Huffman codes (header fields, extra bits)
    // are packed least-significant-bit first (RFC 1951 3.1.1).
    void Bits(unsigned v, int len) { for (int i = 0; i < len; ++i) Bit(static_cast<int>((v >> i) & 1)); }
    // Huffman codes themselves are packed most-significant-bit first
    // (RFC 1951 3.1.1).
    void Code(unsigned v, int len) { for (int i = len - 1; i >= 0; --i) Bit(static_cast<int>((v >> i) & 1)); }
    void Flush() { if (nbits) { bytes.push_back(static_cast<unsigned char>(cur)); cur = 0; nbits = 0; } }
  };

  // RFC 1951's fixed Huffman code lengths (3.2.6) run through the
  // standard canonical-code construction (3.2.2) - the same construction
  // Huffman::Build performs on the decode side in Inflate.cpp, so any code
  // built this way is necessarily one InflateFixed can decode.
  auto CanonicalCodes = [](const std::vector<int>& lengths) {
    int count[16] = {};
    for (int len : lengths) if (len) ++count[len];
    int next_code[16] = {};
    int code = 0;
    for (int len = 1; len < 16; ++len) { code = (code + count[len - 1]) << 1; next_code[len] = code; }
    std::vector<int> codes(lengths.size(), 0);
    for (size_t sym = 0; sym < lengths.size(); ++sym) if (lengths[sym]) codes[sym] = next_code[lengths[sym]]++;
    return codes;
  };
  std::vector<int> lit_len(288);
  for (int i = 0; i < 144; ++i) lit_len[static_cast<size_t>(i)] = 8;
  for (int i = 144; i < 256; ++i) lit_len[static_cast<size_t>(i)] = 9;
  for (int i = 256; i < 280; ++i) lit_len[static_cast<size_t>(i)] = 7;
  for (int i = 280; i < 288; ++i) lit_len[static_cast<size_t>(i)] = 8;
  const std::vector<int> dist_len(30, 5);
  const std::vector<int> lit_code = CanonicalCodes(lit_len);
  const std::vector<int> dist_code = CanonicalCodes(dist_len);

  BitWriter bw;
  bw.Bits(1, 1);  // BFINAL
  bw.Bits(1, 2);  // BTYPE = 01 (fixed Huffman)
  bw.Code(static_cast<unsigned>(lit_code[byte]), lit_len[byte]);  // the one literal byte
  for (size_t i = 0; i < repeats; ++i) {
    bw.Code(static_cast<unsigned>(lit_code[285]), lit_len[285]);  // length symbol 285: base 258, 0 extra bits
    bw.Code(static_cast<unsigned>(dist_code[0]), dist_len[0]);    // distance symbol 0: base 1, 0 extra bits
  }
  bw.Code(static_cast<unsigned>(lit_code[256]), lit_len[256]);  // end-of-block
  bw.Flush();
  return bw.bytes;
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

  // The real decompression-bomb mechanism: a ~1.7 MB fixed-Huffman stream
  // of back-references whose true decoded size (~270 MB) is far beyond a
  // 1 MB cap. The cap must stop the decode as it happens, not just notice
  // the overrun after the fact.
  {
    const size_t repeats = 1100000;  // 1 + 258*1,100,000 ~= 270.6 MiB if unbounded
    const std::vector<unsigned char> block = FixedHuffmanRunBlock('A', repeats);
    std::vector<unsigned char> out;
    Check(!InflateRaw(block.data(), block.size(), out, 1 << 20), "a fixed-Huffman back-reference bomb capped well below its true size is rejected");
    Check(out.size() <= (1 << 20), "...and never grew past the cap while decoding it");
  }

  // Same stream, given enough room: it must decode correctly in full, so
  // the rejection above is really the cap at work and not a malformed
  // stream that happens to fail for the wrong reason.
  {
    const size_t repeats = 2000;
    const size_t expect_size = 1 + 258 * repeats;
    const std::vector<unsigned char> block = FixedHuffmanRunBlock('A', repeats);
    std::vector<unsigned char> out;
    Check(InflateRaw(block.data(), block.size(), out, expect_size), "the same kind of fixed-Huffman back-reference stream decodes correctly when given enough room");
    Check(out.size() == expect_size, "...producing exactly the expected byte count");
    bool all_a = true;
    for (unsigned char c : out) if (c != 'A') { all_a = false; break; }
    Check(all_a, "...and the expected content throughout");
  }

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
