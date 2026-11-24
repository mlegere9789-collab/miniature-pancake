#include "io/DigitalSignature.h"

#include "app/Settings.h"
#include "util/BigUint.h"
#include "util/Sha256.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace dino8::app {

namespace {

namespace fs = std::filesystem;
using dino8::util::BigUint;

constexpr const char* kPublicExponentHex = "10001";  // 65537, the standard fixed RSA public exponent.

// The ASN.1 DigestInfo DER prefix PKCS#1 v1.5 (RFC 8017 section 9.2, Note
// 1) specifies for SHA-256, ahead of the raw 32-byte digest itself - the
// fixed bytes every SHA-256 PKCS#1v1.5 signature embeds, independent of
// which file or key is involved.
constexpr const char* kSha256DigestInfoPrefixHex = "3031300d060960864801650304020105000420";

std::string ToLowerHex(const std::string& s) {
  std::string out = s;
  for (char& c : out) if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

// Zero-pads `v`'s hex representation on the left to exactly `byte_len` bytes
// (2*byte_len hex characters) - the fixed-width big-endian encoding RSA
// signatures/messages require (BigUint::ToHex() itself strips leading
// zeros, since it has no notion of a target width).
std::string ToFixedHex(const BigUint& v, size_t byte_len) {
  std::string h = ToLowerHex(v.ToHex());
  const size_t want = byte_len * 2;
  if (h.size() > want) throw std::invalid_argument("ToFixedHex: value too large for byte_len");
  if (h.size() < want) h = std::string(want - h.size(), '0') + h;
  return h;
}

bool ReadWholeFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// Builds the PKCS#1 v1.5 encoded message EM = 00 || 01 || PS(ff..) || 00 ||
// DigestInfo(SHA256) || hash, as a hex string exactly 2*modulus_bytes long
// - see RFC 8017 section 9.2 (EMSA-PKCS1-v1_5-ENCODE). `modulus_bytes` must
// be at least 11 + 19 + 32 (=62) bytes for there to be room for at least
// the mandatory 8 bytes of 0xff padding; every RSA size this app generates
// (2048 bits = 256 bytes) has ample room.
bool BuildPkcs1DigestMessage(const std::string& sha256_hex, size_t modulus_bytes, std::string& em_hex,
                              std::string& error) {
  const size_t t_len = std::string(kSha256DigestInfoPrefixHex).size() / 2 + 32;
  if (modulus_bytes < t_len + 11) { error = "RSA modulus too small for a SHA-256 PKCS#1v1.5 signature"; return false; }
  const size_t ps_len = modulus_bytes - 3 - t_len;
  em_hex = "0001" + std::string(ps_len * 2, 'f') + "00" + kSha256DigestInfoPrefixHex + ToLowerHex(sha256_hex);
  return em_hex.size() == modulus_bytes * 2;
}

size_t HexByteLen(const std::string& hex) { return (hex.size() + 1) / 2; }

}  // namespace

RsaPrivateKey GenerateRsaKeyPair(int bits) {
  if (bits < 128 || bits % 2 != 0) throw std::invalid_argument("GenerateRsaKeyPair: bits must be even and >= 128");
  const int half = bits / 2;
  const BigUint e = BigUint::FromHex(kPublicExponentHex);
  BigUint p, q, phi, n;
  for (;;) {
    p = BigUint::GenerateProbablePrime(half);
    if (!BigUint::Mod(BigUint::Sub(p, BigUint(1)), e).IsZero()) break;
  }
  for (;;) {
    q = BigUint::GenerateProbablePrime(half);
    if (q == p) continue;
    if (!BigUint::Mod(BigUint::Sub(q, BigUint(1)), e).IsZero()) break;
  }
  n = BigUint::Mul(p, q);
  phi = BigUint::Mul(BigUint::Sub(p, BigUint(1)), BigUint::Sub(q, BigUint(1)));
  BigUint d;
  if (!util::ModInverse(e, phi, d)) throw std::runtime_error("GenerateRsaKeyPair: e has no inverse mod phi (should not happen for prime e and freshly generated p, q)");
  RsaPrivateKey key;
  key.pub.n_hex = n.ToHex();
  key.pub.e_hex = e.ToHex();
  key.d_hex = d.ToHex();
  return key;
}

namespace {

// Hand-written JSON read/write for the signing-key file and .sig sidecars
// - the same "no third-party JSON dependency, just enough to round-trip
// our own fixed schema" approach util/json_mini.h already takes for
// reading, and the plain std::ofstream "{" ... "}" writing
// input/SpaceMouse.cpp's config save already uses; every field here is a
// hex string or an algorithm name, never free text that would need real
// escaping.
std::string JsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
  return out;
}

// Extracts the string value of `"key": "value"` from `text` - deliberately
// narrow (every value this file ever writes is a bare hex/ASCII string
// with no embedded quote or backslash) rather than pulling in json_mini
// just for four fixed fields.
bool ExtractJsonString(const std::string& text, const std::string& key, std::string& out) {
  const std::string needle = "\"" + key + "\"";
  size_t pos = text.find(needle);
  if (pos == std::string::npos) return false;
  pos = text.find(':', pos + needle.size());
  if (pos == std::string::npos) return false;
  pos = text.find('"', pos);
  if (pos == std::string::npos) return false;
  const size_t end = text.find('"', pos + 1);
  if (end == std::string::npos) return false;
  out = text.substr(pos + 1, end - pos - 1);
  return true;
}

std::string SigningKeyPath() { return (fs::path(ConfigDirectory()) / "signing_key.json").string(); }

}  // namespace

bool LoadOrCreateSigningKey(RsaPrivateKey& out, std::string& error) {
  const std::string path = SigningKeyPath();
  std::string text;
  if (ReadWholeFile(path, text)) {
    if (!ExtractJsonString(text, "n", out.pub.n_hex) || !ExtractJsonString(text, "e", out.pub.e_hex) ||
        !ExtractJsonString(text, "d", out.d_hex)) {
      error = "Corrupt signing key file: " + path;
      return false;
    }
    return true;
  }
  out = GenerateRsaKeyPair();
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream f(path);
  if (!f) { error = "Could not write signing key: " + path; return false; }
  f << "{\n"
    << "  \"algorithm\": \"RSA-2048/SHA256/PKCS1v1.5\",\n"
    << "  \"n\": \"" << JsonEscape(out.pub.n_hex) << "\",\n"
    << "  \"e\": \"" << JsonEscape(out.pub.e_hex) << "\",\n"
    << "  \"d\": \"" << JsonEscape(out.d_hex) << "\"\n"
    << "}\n";
  f.close();
  // This file holds the private exponent `d` - restrict it to owner
  // read/write only. std::ofstream's default mode is world-readable on
  // POSIX (typically 0644), which would let any other local account read
  // the private key and forge signatures under this machine's identity;
  // a failure here (e.g. an unsupported filesystem) is silently ignored
  // rather than treated as fatal, matching Settings.cpp's own tolerant
  // "best-effort config directory" stance elsewhere.
  std::error_code ec;
  fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
  return true;
}

std::string SignFileHash(const std::string& path, const RsaPrivateKey& key, std::string& error) {
  std::string hash_hex;
  if (!util::Sha256HexOfFile(path, hash_hex, error)) return "";
  const BigUint n = BigUint::FromHex(key.pub.n_hex);
  const size_t modulus_bytes = HexByteLen(ToLowerHex(n.ToHex()));
  std::string em_hex;
  if (!BuildPkcs1DigestMessage(hash_hex, modulus_bytes, em_hex, error)) return "";
  const BigUint m = BigUint::FromHex(em_hex);
  const BigUint d = BigUint::FromHex(key.d_hex);
  const BigUint s = BigUint::ModPow(m, d, n);
  return ToFixedHex(s, modulus_bytes);
}

bool VerifyFileSignature(const std::string& path, const std::string& signature_hex, const RsaPublicKey& pub,
                          std::string& error) {
  std::string hash_hex;
  if (!util::Sha256HexOfFile(path, hash_hex, error)) return false;
  const BigUint n = BigUint::FromHex(pub.n_hex);
  const BigUint e = BigUint::FromHex(pub.e_hex);
  const size_t modulus_bytes = HexByteLen(ToLowerHex(n.ToHex()));
  BigUint s;
  try {
    s = BigUint::FromHex(signature_hex);
  } catch (const std::exception&) {
    error = "Malformed signature (not hex)";
    return false;
  }
  if (s >= n) { error = "Malformed signature (>= modulus)"; return false; }
  const BigUint recovered = BigUint::ModPow(s, e, n);
  const std::string em_hex = ToFixedHex(recovered, modulus_bytes);
  std::string expected_em_hex;
  if (!BuildPkcs1DigestMessage(hash_hex, modulus_bytes, expected_em_hex, error)) return false;
  if (em_hex != expected_em_hex) { error = "Signature does not match this file/key"; return false; }
  return true;
}

bool WriteSignatureSidecar(const std::string& path, const RsaPrivateKey& key, std::string& error) {
  const std::string sig = SignFileHash(path, key, error);
  if (sig.empty()) return false;
  std::string hash_hex;
  if (!util::Sha256HexOfFile(path, hash_hex, error)) return false;
  const std::string sig_path = path + ".sig";
  std::ofstream f(sig_path);
  if (!f) { error = "Could not write " + sig_path; return false; }
  f << "{\n"
    << "  \"algorithm\": \"RSA-2048/SHA256/PKCS1v1.5\",\n"
    << "  \"file\": \"" << JsonEscape(fs::path(path).filename().string()) << "\",\n"
    << "  \"sha256\": \"" << JsonEscape(hash_hex) << "\",\n"
    << "  \"public_key\": { \"n\": \"" << JsonEscape(key.pub.n_hex) << "\", \"e\": \"" << JsonEscape(key.pub.e_hex) << "\" },\n"
    << "  \"signature\": \"" << JsonEscape(sig) << "\"\n"
    << "}\n";
  return true;
}

bool VerifySignatureSidecar(const std::string& path, const std::string& sig_path_in, std::string& signer_fingerprint,
                             std::string& error) {
  const std::string sig_path = sig_path_in.empty() ? path + ".sig" : sig_path_in;
  std::string text;
  if (!ReadWholeFile(sig_path, text)) { error = "Could not read " + sig_path; return false; }
  RsaPublicKey pub;
  std::string signature_hex;
  if (!ExtractJsonString(text, "n", pub.n_hex) || !ExtractJsonString(text, "e", pub.e_hex) ||
      !ExtractJsonString(text, "signature", signature_hex)) {
    error = "Corrupt signature file: " + sig_path;
    return false;
  }
  if (!VerifyFileSignature(path, signature_hex, pub, error)) return false;
  signer_fingerprint = util::Sha256Hex(pub.n_hex + ":" + pub.e_hex);
  return true;
}

}  // namespace dino8::app
