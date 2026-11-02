// Digital signing of exported files (PARITY_MAP.md's "Digital signing of
// exported files" - no file-signing code existed anywhere before this).
// A real RSA/SHA-256/PKCS#1v1.5 signature scheme, built entirely on
// util/BigUint.h and the existing util/Sha256.h - no external crypto
// library. `DigitalSign` hashes a file and signs the digest with a locally
// generated RSA private key; `VerifySignature` recomputes the file's hash
// and checks it against a signature using the matching public key. Genuine
// asymmetric cryptography (the public key alone can verify but not forge a
// signature) rather than a checksum/HMAC - the actual gap this codebase's
// own plug-in marketplace (src/plugins/Marketplace.cpp) already documents
// on its own SHA-256 integrity check: "the only integrity check available
// ... since there is no code-signing here".
//
// The signing keypair is 2048-bit RSA - two from-scratch 1024-bit
// Miller-Rabin-tested primes (see BigUint::GenerateProbablePrime) - a
// real, modern key size, not a toy one shrunk for speed; key generation is
// a one-time cost (a few seconds) paid once per machine, not per signature.
// Both scheme and key size are the caller's choice to change later, but
// this is a genuine, unshrunk RSA-2048 by default.
#pragma once

#include <string>

namespace dino8::app {

struct RsaPublicKey {
  std::string n_hex;  // modulus, hex, no leading "0x"
  std::string e_hex;  // public exponent (65537, i.e. "10001"), hex
};

struct RsaPrivateKey {
  RsaPublicKey pub;
  std::string d_hex;  // private exponent, hex
};

// Generates a fresh RSA keypair. `bits` is the modulus size (must be even;
// each of the two generated primes gets bits/2). Real prime generation -
// candidate sieving against small primes, then Miller-Rabin - not a fixed
// or precomputed pair, so this genuinely differs every call.
RsaPrivateKey GenerateRsaKeyPair(int bits = 2048);

// Loads/creates this machine's persistent signing key at
// `<ConfigDirectory()>/signing_key.json` (see app/Settings.h), generating
// one with GenerateRsaKeyPair() and saving it the first time this is
// called. Returns false (with `error` set) only if the file exists but is
// unreadable/corrupt, or can't be written on first creation.
bool LoadOrCreateSigningKey(RsaPrivateKey& out, std::string& error);

// Signs the file at `path`: SHA-256 of its bytes, PKCS#1 v1.5-padded and
// raised to the private exponent mod n. Returns the signature as a fixed-
// width (matching the modulus' own byte length) lower-case hex string, or
// "" with `error` set if the file can't be read.
std::string SignFileHash(const std::string& path, const RsaPrivateKey& key, std::string& error);

// Verifies `signature_hex` (as produced by SignFileHash) against the file
// at `path` and `pub`. Returns true only if the file can be read, the
// signature's PKCS#1 v1.5 structure is intact once raised to the public
// exponent, and the digest embedded in it equals the file's own current
// SHA-256 - so a re-exported/edited file, a truncated signature, or the
// wrong public key all fail closed, not open.
bool VerifyFileSignature(const std::string& path, const std::string& signature_hex, const RsaPublicKey& pub,
                          std::string& error);

// Writes a `.sig` sidecar next to `path` (JSON: algorithm, the *public*
// key, the signature, and the signed file's own SHA-256 for a quick
// human-readable integrity note) - never the private key. Returns false
// with `error` set if `path` can't be read or the sidecar can't be
// written.
bool WriteSignatureSidecar(const std::string& path, const RsaPrivateKey& key, std::string& error);

// Reads a `.sig` sidecar written by WriteSignatureSidecar (defaulting to
// `path + ".sig"` when `sig_path` is empty) and verifies it against `path`.
// `signer_fingerprint` is set to the SHA-256 hex of the sidecar's own
// public-key fields - not proof of identity by itself (there is no
// certificate authority here), but a stable value the same signer's other
// files can be compared against, and that can be checked out-of-band
// against a key the signer published elsewhere. Returns false with `error`
// set on a missing/corrupt sidecar, a missing/unreadable `path`, or a
// signature that fails VerifyFileSignature.
bool VerifySignatureSidecar(const std::string& path, const std::string& sig_path, std::string& signer_fingerprint,
                             std::string& error);

}  // namespace dino8::app
