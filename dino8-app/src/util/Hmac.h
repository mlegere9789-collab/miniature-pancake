// HMAC-SHA256 (RFC 2104 / FIPS 198-1) on top of util/Sha256.h, header-only so a standalone test target only needs Sha256.cpp.
// Used by net/Collab.cpp's token handshake: the shared token itself never crosses the wire, only HMAC(token, nonce).
#pragma once

#include <string>

#include "util/Sha256.h"

namespace dino8::util {

namespace hmac_detail {
inline std::string HexToBytes(const std::string& hex) {
  std::string out;
  out.reserve(hex.size() / 2);
  auto nib = [](char c) { return c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'; };
  for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<char>((nib(hex[i]) << 4) | nib(hex[i + 1])));
  return out;
}
}  // namespace hmac_detail

// Lower-case hex of HMAC-SHA256(key, message); `key` and `message` may hold any bytes.
inline std::string HmacSha256Hex(const std::string& key, const std::string& message) {
  const size_t block = 64;
  std::string k = key.size() > block ? hmac_detail::HexToBytes(Sha256Hex(key)) : key;
  k.resize(block, '\0');
  std::string ipad(block, '\0'), opad(block, '\0');
  for (size_t i = 0; i < block; ++i) {
    ipad[i] = static_cast<char>(k[i] ^ 0x36);
    opad[i] = static_cast<char>(k[i] ^ 0x5c);
  }
  const std::string inner = hmac_detail::HexToBytes(Sha256Hex(ipad + message));
  return Sha256Hex(opad + inner);
}

// Compares without stopping at the first differing byte, so a mismatch does not leak how long the matching prefix was.
inline bool ConstantTimeEqual(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

}  // namespace dino8::util
