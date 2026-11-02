// A minimal arbitrary-precision unsigned integer, built only for
// DigitalSignature.cpp's RSA key generation/sign/verify math (see
// io/DigitalSignature.h): schoolbook add/sub/mul/divmod, modular
// exponentiation (binary square-and-multiply), and a cryptographic-strength
// random generator for candidate primes. Deliberately not a general bignum
// library - no operator overloads beyond what RSA needs, no negative
// numbers (see BigInt below for the signed wrapper the extended-Euclidean
// modular inverse needs), no optimized multiplication (Karatsuba/FFT) since
// even a 2048-bit RSA keygen's handful of Miller-Rabin rounds finishes in a
// fraction of a second with plain O(n*m) schoolbook multiply at this scale.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dino8::util {

// Little-endian base-2^32 limbs (limbs[0] is the least significant word),
// with no leading (most-significant) zero limbs except that zero itself is
// represented as an empty limb vector - the single normalization invariant
// every method below maintains, so comparison can just compare limb counts
// then compare limbs from the top down.
class BigUint {
 public:
  BigUint() = default;
  explicit BigUint(std::uint64_t v);

  static BigUint FromHex(const std::string& hex);
  std::string ToHex() const;

  bool IsZero() const { return limbs_.empty(); }
  bool IsEven() const { return limbs_.empty() || (limbs_[0] & 1u) == 0; }
  // Number of significant bits (0 for zero), the size Miller-Rabin's
  // random-candidate generation and modpow's bit-by-bit exponent walk
  // both need.
  int BitLength() const;
  bool GetBit(int i) const;

  static int Compare(const BigUint& a, const BigUint& b);
  friend bool operator==(const BigUint& a, const BigUint& b) { return Compare(a, b) == 0; }
  friend bool operator!=(const BigUint& a, const BigUint& b) { return Compare(a, b) != 0; }
  friend bool operator<(const BigUint& a, const BigUint& b) { return Compare(a, b) < 0; }
  friend bool operator<=(const BigUint& a, const BigUint& b) { return Compare(a, b) <= 0; }
  friend bool operator>(const BigUint& a, const BigUint& b) { return Compare(a, b) > 0; }
  friend bool operator>=(const BigUint& a, const BigUint& b) { return Compare(a, b) >= 0; }

  static BigUint Add(const BigUint& a, const BigUint& b);
  // Requires a >= b (unsigned - there is no representable negative result).
  static BigUint Sub(const BigUint& a, const BigUint& b);
  static BigUint Mul(const BigUint& a, const BigUint& b);
  // Schoolbook long division. Requires a non-zero divisor.
  static void DivMod(const BigUint& a, const BigUint& b, BigUint& quotient, BigUint& remainder);
  static BigUint Mod(const BigUint& a, const BigUint& m) { BigUint q, r; DivMod(a, m, q, r); return r; }

  // base^exp mod m, by binary square-and-multiply. Requires a non-zero m.
  static BigUint ModPow(const BigUint& base, const BigUint& exp, const BigUint& m);

  // A cryptographically-random odd value with exactly `bits` significant
  // bits (top and bottom bit both forced to 1) - GenerateProbablePrime's
  // own candidate shape, pulled out so tests can also exercise it directly.
  static BigUint RandomOdd(int bits);

  // Fermat/Miller-Rabin composite check: false means definitely composite,
  // true means probably prime (rounds=20 gives a false-positive probability
  // under 2^-40, the same margin OpenSSL's own default targets).
  static bool IsProbablePrime(const BigUint& n, int rounds = 20);
  // Draws random `bits`-bit odd candidates until IsProbablePrime accepts
  // one (rejecting small-prime multiples first, the same cheap sieve every
  // real implementation runs before paying for Miller-Rabin).
  static BigUint GenerateProbablePrime(int bits);

 private:
  std::vector<std::uint32_t> limbs_;

  void Trim();
};

// A signed wrapper around BigUint - just enough arithmetic (add, sub, mul)
// for ModInverse's extended-Euclidean back-substitution below, which
// genuinely needs negative intermediate values (Bezout coefficients
// alternate sign every step) even though every public RSA type (modulus,
// exponent, signature) is unsigned.
class BigInt {
 public:
  BigInt() = default;
  BigInt(bool negative, BigUint magnitude) : negative_(magnitude.IsZero() ? false : negative), mag_(std::move(magnitude)) {}
  static BigInt FromUnsigned(const BigUint& v) { return BigInt(false, v); }

  bool IsNegative() const { return negative_; }
  const BigUint& Magnitude() const { return mag_; }

  static BigInt Add(const BigInt& a, const BigInt& b);
  static BigInt Sub(const BigInt& a, const BigInt& b);
  static BigInt Mul(const BigInt& a, const BigInt& b);

  // Reduces `v` into [0, m) - the last step ModInverse needs, since its
  // raw Bezout coefficient can be negative.
  static BigUint ModNonNegative(const BigInt& v, const BigUint& m);

 private:
  bool negative_ = false;
  BigUint mag_;
};

// x such that (a * x) mod m == 1, in [0, m). Returns false (leaving `out`
// unset) if gcd(a, m) != 1, i.e. no inverse exists - RSA key generation
// checks this when picking e (it never should fail for the fixed e=65537
// this codebase uses against a real random prime pair, but a genuine
// mathematical precondition gets a real failure path, not an assumption).
bool ModInverse(const BigUint& a, const BigUint& m, BigUint& out);

}  // namespace dino8::util
