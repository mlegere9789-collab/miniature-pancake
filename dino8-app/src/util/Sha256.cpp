#include "util/Sha256.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace dino8::util {

namespace {

constexpr std::array<uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t RotR(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

class Sha256 {
 public:
  Sha256() { h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}; }

  void Update(const unsigned char* data, size_t len) {
    total_len_ += len;
    while (len > 0) {
      const size_t take = std::min(len, size_t(64) - buf_len_);
      std::memcpy(buf_.data() + buf_len_, data, take);
      buf_len_ += take;
      data += take;
      len -= take;
      if (buf_len_ == 64) {
        ProcessBlock(buf_.data());
        buf_len_ = 0;
      }
    }
  }

  std::string HexDigest() {
    // Padding: 0x80, zeros, then the 64-bit bit-length, big-endian.
    const uint64_t bit_len = total_len_ * 8;
    unsigned char pad = 0x80;
    Update(&pad, 1);
    unsigned char zero = 0;
    while (buf_len_ != 56) Update(&zero, 1);
    unsigned char len_be[8];
    for (int i = 0; i < 8; ++i) len_be[i] = static_cast<unsigned char>(bit_len >> (56 - 8 * i));
    Update(len_be, 8);

    std::ostringstream out;
    out << std::hex << std::nouppercase;
    for (uint32_t word : h_) {
      char buf[9];
      std::snprintf(buf, sizeof buf, "%08x", word);
      out << buf;
    }
    return out.str();
  }

 private:
  void ProcessBlock(const unsigned char* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t temp1 = hh + s1 + ch + kK[i] + w[i];
      const uint32_t s0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + maj;
      hh = g; g = f; f = e; e = d + temp1;
      d = c; c = b; b = a; a = temp1 + temp2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
  }

  std::array<uint32_t, 8> h_{};
  std::array<unsigned char, 64> buf_{};
  size_t buf_len_ = 0;
  uint64_t total_len_ = 0;
};

}  // namespace

std::string Sha256Hex(const std::string& data) {
  Sha256 s;
  s.Update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
  return s.HexDigest();
}

bool Sha256HexOfFile(const std::string& path, std::string& out, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "could not open " + path + " to hash it";
    return false;
  }
  Sha256 s;
  unsigned char buf[8192];
  while (in) {
    in.read(reinterpret_cast<char*>(buf), sizeof buf);
    const std::streamsize got = in.gcount();
    if (got > 0) s.Update(buf, static_cast<size_t>(got));
  }
  out = s.HexDigest();
  return true;
}

}  // namespace dino8::util
