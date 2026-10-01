// A small, dependency-free RFC 1951 DEFLATE decoder plus the standard
// CRC-32 (IEEE 802.3) checksum both PNG and ZIP use - shared by
// render/ImageIO.cpp (PNG's IDAT stream, which is this raw deflate bit-
// stream wrapped in a 2-byte zlib/RFC 1950 header) and drafting/Xlsx.cpp
// (a ZIP entry's "deflate" method, which stores this same raw bit-stream
// with no wrapper at all). Originally written for PNG alone and moved here
// so a second, independent format reader doesn't have to duplicate or
// re-derive it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dino8::util {

// Standard CRC-32 (IEEE 802.3 / zlib/ZIP/PNG polynomial, reflected,
// 0xFFFFFFFF init/final-xor).
uint32_t Crc32(const unsigned char* data, size_t n);

// The default (and recommended sane ceiling) for `max_output` below. A
// caller that has its own untrusted "expected decoded size" field (a ZIP
// entry's declared uncompressed size, say) should clamp it against this
// constant rather than pass it through unclamped - that field lives in
// attacker-controlled input right alongside the compressed bytes, so
// trusting it as-is just moves the attacker-chosen cap from here to there.
constexpr size_t kDefaultMaxInflateOutput = 256u * 1024 * 1024;

// Raw DEFLATE (RFC 1951) decode - no zlib (RFC 1950) or gzip wrapper, just
// the deflate bit-stream itself (a sequence of stored/fixed-Huffman/
// dynamic-Huffman blocks). Appends decoded bytes to `out`. Returns false on
// a malformed or truncated stream, or one whose decoded size would exceed
// `max_output` - a deflate stream can expand its input by three orders of
// magnitude via back-references, so a caller that knows the expected
// decoded size (a ZIP entry's declared uncompressed size, a PNG's
// width*height) should pass it here rather than let a small malicious
// input decompress to gigabytes before any size check downstream ever runs.
// That expected size should itself be clamped to kDefaultMaxInflateOutput
// first if it comes from the untrusted input rather than from the caller's
// own already-validated state.
bool InflateRaw(const unsigned char* data, size_t size, std::vector<unsigned char>& out, size_t max_output = kDefaultMaxInflateOutput);

}  // namespace dino8::util
