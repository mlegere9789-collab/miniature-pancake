// See Inflate.h. Moved verbatim (BitReader/Huffman/InflateStored/
// InflateFixed/InflateDynamic/InflateCodes, plus Crc32) out of
// render/ImageIO.cpp's own file-local "zlib inflate (RFC 1951) - a compact
// 'puff'-style decoder" section, which now calls InflateRaw (after
// stripping its own 2-byte zlib header) instead of keeping a second copy.
#include "util/Inflate.h"

namespace dino8::util {

namespace {

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

}  // namespace

uint32_t Crc32(const unsigned char* data, size_t n) {
  static uint32_t table[256];
  static bool built = false;
  if (!built) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    built = true;
  }
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

bool InflateRaw(const unsigned char* data, size_t size, std::vector<unsigned char>& out) {
  BitReader br{data, size};
  int last = 0;
  do {
    last = br.Bits(1);
    const int type = br.Bits(2);
    bool ok = false;
    if (type == 0) ok = InflateStored(br, out);
    else if (type == 1) ok = InflateFixed(br, out);
    else if (type == 2) ok = InflateDynamic(br, out);
    if (!ok || br.overrun) return false;
  } while (!last);
  return true;
}

}  // namespace dino8::util
