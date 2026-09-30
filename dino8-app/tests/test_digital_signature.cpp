// Verifies io/DigitalSignature.h's RSA/SHA-256/PKCS#1v1.5 file-signing
// scheme (PARITY_MAP.md's "Digital signing of exported files") end to end:
// util/BigUint.h's arithmetic against the classic textbook RSA example
// (p=61, q=53, e=17 - Wikipedia's own "RSA (cryptosystem)" worked example,
// chosen because it's independently checkable by hand, not because this
// codebase's real keys are anywhere near that small), a real generated
// keypair signing and verifying a real file, and the failure paths a
// signature scheme actually exists to catch: a file edited after signing,
// and a signature checked against the wrong public key.
//
// Provides its own trivial dino8::app::ConfigDirectory() stub (a fixed
// scratch path) rather than linking the real app/Settings.cpp, which pulls
// in app/Application.h's full class definition - this test only exercises
// GenerateRsaKeyPair/SignFileHash/VerifyFileSignature/
// WriteSignatureSidecar/VerifySignatureSidecar directly and never calls
// LoadOrCreateSigningKey, so the stub's exact path is never read from.
#include "io/DigitalSignature.h"
#include "util/BigUint.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace dino8::app {
std::string ConfigDirectory() { return "/tmp/dino8_test_config_unused"; }
}  // namespace dino8::app

using dino8::util::BigUint;
using dino8::app::GenerateRsaKeyPair;
using dino8::app::RsaPrivateKey;
using dino8::app::SignFileHash;
using dino8::app::VerifyFileSignature;
using dino8::app::VerifySignatureSidecar;
using dino8::app::WriteSignatureSidecar;

namespace {
namespace fs = std::filesystem;
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

void WriteFile(const std::string& path, const std::string& content) {
  std::ofstream f(path, std::ios::binary);
  f << content;
}
}  // namespace

int main() {
  // ---- BigUint arithmetic -------------------------------------------------
  Check(BigUint::Add(BigUint::FromHex("ff"), BigUint(1)) == BigUint::FromHex("100"), "Add: 0xff + 1 == 0x100");
  Check(BigUint::Sub(BigUint::FromHex("100"), BigUint(1)) == BigUint::FromHex("ff"), "Sub: 0x100 - 1 == 0xff");
  Check(BigUint::Mul(BigUint(1234), BigUint(5678)) == BigUint(1234u * 5678u), "Mul: 1234 * 5678");
  {
    BigUint q, r;
    BigUint::DivMod(BigUint(17), BigUint(5), q, r);
    Check(q == BigUint(3) && r == BigUint(2), "DivMod: 17 / 5 == 3 remainder 2");
  }
  Check(BigUint::ModPow(BigUint(2), BigUint(10), BigUint(1000)) == BigUint(24), "ModPow: 2^10 mod 1000 == 24");

  // ---- Classic textbook RSA example (Wikipedia "RSA (cryptosystem)"): -----
  // p=61, q=53, n=3233, phi=3120, e=17, d=2753.
  {
    const BigUint n(3233), e(17), d(2753), phi(3120), m(65);
    const BigUint c = BigUint::ModPow(m, e, n);
    Check(c == BigUint(2790), "textbook RSA: 65^17 mod 3233 == 2790");
    Check(BigUint::ModPow(c, d, n) == m, "textbook RSA: 2790^2753 mod 3233 == 65 (decrypts back)");
    BigUint computed_d;
    Check(dino8::util::ModInverse(e, phi, computed_d) && computed_d == d, "ModInverse: 17^-1 mod 3120 == 2753");
  }

  // ---- Full keypair generation + file signing round trip ------------------
  // A small (768-bit) modulus, deliberately shrunk from this feature's real
  // 2048-bit default purely so keygen's Miller-Rabin search finishes in
  // milliseconds here - the RSA/PKCS#1v1.5 code path exercised is identical
  // either way (see GenerateRsaKeyPair's own doc comment on the 2048-bit
  // default). Must stay >= 496 bits (62 bytes: PKCS#1v1.5's own 11-byte
  // fixed overhead plus the 51-byte SHA-256 DigestInfo - see
  // BuildPkcs1DigestMessage's own size check) for there to be room for a
  // SHA-256 signature at all; 768 leaves headroom to spare.
  const fs::path dir = fs::temp_directory_path() / "dino8_digital_signature_test";
  std::error_code ec;
  fs::create_directories(dir, ec);
  const std::string file_path = (dir / "exported_model.xyz").string();
  WriteFile(file_path, "0 0 0\n1 0 0\n2 1 0\n");

  RsaPrivateKey key = GenerateRsaKeyPair(768);
  Check(!key.pub.n_hex.empty() && !key.d_hex.empty(), "GenerateRsaKeyPair: produced a non-empty keypair");

  std::string error;
  const std::string sig = SignFileHash(file_path, key, error);
  Check(!sig.empty(), "SignFileHash: produced a signature");
  Check(VerifyFileSignature(file_path, sig, key.pub, error), "VerifyFileSignature: a fresh signature verifies");

  // Tamper with the file after signing: the digest embedded in the
  // signature no longer matches, so verification must fail closed.
  WriteFile(file_path, "0 0 0\n1 0 0\n2 1 0\n9 9 9\n");
  Check(!VerifyFileSignature(file_path, sig, key.pub, error), "VerifyFileSignature: fails after the file is edited");

  // A second, unrelated keypair's public key must not validate the first
  // key's signature - the actual property that makes this "signing", not
  // just a checksum anyone could recompute and swap in.
  WriteFile(file_path, "0 0 0\n1 0 0\n2 1 0\n");
  RsaPrivateKey other_key = GenerateRsaKeyPair(768);
  Check(!VerifyFileSignature(file_path, sig, other_key.pub, error), "VerifyFileSignature: fails against the wrong public key");

  // A corrupt/non-hex signature string must be rejected, not crash.
  Check(!VerifyFileSignature(file_path, "not hex!", key.pub, error), "VerifyFileSignature: rejects a malformed signature string");

  // ---- .sig sidecar round trip ---------------------------------------------
  Check(WriteSignatureSidecar(file_path, key, error), "WriteSignatureSidecar: writes a sidecar");
  Check(fs::exists(file_path + ".sig"), "WriteSignatureSidecar: the sidecar file actually exists on disk");
  std::string fingerprint;
  Check(VerifySignatureSidecar(file_path, "", fingerprint, error) && !fingerprint.empty(),
        "VerifySignatureSidecar: verifies the file it was written for and returns a fingerprint");
  WriteFile(file_path, "tampered content, different from what was signed\n");
  Check(!VerifySignatureSidecar(file_path, "", fingerprint, error), "VerifySignatureSidecar: fails once the file changes");

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
