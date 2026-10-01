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

// Raw DEFLATE (RFC 1951) decode - no zlib (RFC 1950) or gzip wrapper, just
// the deflate bit-stream itself (a sequence of stored/fixed-Huffman/
// dynamic-Huffman blocks). Appends decoded bytes to `out`. Returns false on
// a malformed or truncated stream, or one whose decoded size would exceed
// `max_output` - a deflate stream can expand its input by three orders of
// magnitude via back-references, so a caller that knows the expected
// decoded size (a ZIP entry's declared uncompressed size, a PNG's
// width*height) should pass it here rather than let a small malicious
// input decompress to gigabytes before any size check downstream ever runs.
bool InflateRaw(const unsigned char* data, size_t size, std::vector<unsigned char>& out, size_t max_output = 256u * 1024 * 1024);

}  // namespace dino8::util
