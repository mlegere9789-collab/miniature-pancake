#include "util/BigUint.h"

#include <algorithm>
#include <array>
#include <random>
#include <stdexcept>

namespace dino8::util {

namespace {

// A fixed random_device-seeded generator, re-seeded per call so successive
// GenerateProbablePrime() calls (as RSA key generation makes two, for p and
// q) don't share state in a way that could correlate their outputs.
std::uint32_t SeededWord() {
  static std::random_device rd;
  return rd();
}

// The small odd primes RSA's own key-generation trial-division sieve checks
// candidates against before paying for a full Miller-Rabin round - the same
// "cheap filter before the expensive test" two-stage shape most real
// implementations (OpenSSL included) use, since the overwhelming majority
// of random odd candidates are divisible by one of these.
const std::array<std::uint32_t, 54> kSmallPrimes = {
    2,   3,   5,   7,   11,  13,  17,  19,  23,  29,  31,  37,  41,  43,  47,  53,  59,  61,
    67,  71,  73,  79,  83,  89,  97,  101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151,
    157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251};

}  // namespace

BigUint::BigUint(std::uint64_t v) {
  if (v == 0) return;
  limbs_.push_back(static_cast<std::uint32_t>(v & 0xffffffffu));
  if (v >> 32) limbs_.push_back(static_cast<std::uint32_t>(v >> 32));
}

void BigUint::Trim() {
  while (!limbs_.empty() && limbs_.back() == 0) limbs_.pop_back();
}

BigUint BigUint::FromHex(const std::string& hex_in) {
  std::string hex = hex_in;
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex = hex.substr(2);
  BigUint out;
  // 8 hex digits per 32-bit limb, filled from the least-significant end.
  int pos = static_cast<int>(hex.size());
  while (pos > 0) {
    const int start = std::max(0, pos - 8);
    const std::string chunk = hex.substr(static_cast<size_t>(start), static_cast<size_t>(pos - start));
    std::uint32_t limb = 0;
    for (char c : chunk) {
      limb <<= 4;
      if (c >= '0' && c <= '9') limb |= static_cast<std::uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') limb |= static_cast<std::uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') limb |= static_cast<std::uint32_t>(c - 'A' + 10);
      else throw std::invalid_argument("BigUint::FromHex: not a hex digit");
    }
    out.limbs_.push_back(limb);
    pos = start;
  }
  out.Trim();
  return out;
}

std::string BigUint::ToHex() const {
  if (limbs_.empty()) return "0";
  static const char* kDigits = "0123456789abcdef";
  std::string out;
  char buf[9];
  buf[8] = '\0';
  for (size_t i = limbs_.size(); i-- > 0;) {
    std::uint32_t limb = limbs_[i];
    for (int d = 7; d >= 0; --d) { buf[d] = kDigits[limb & 0xf]; limb >>= 4; }
    if (i + 1 == limbs_.size()) {
      // Leading limb: skip its own leading zero nibbles, but keep at least
      // one digit even if the leading limb is itself small.
      const char* p = buf;
      while (*p == '0' && *(p + 1) != '\0') ++p;
      out += p;
    } else {
      out += buf;
    }
  }
  return out;
}

int BigUint::BitLength() const {
  if (limbs_.empty()) return 0;
  std::uint32_t top = limbs_.back();
  int bits = static_cast<int>(limbs_.size() - 1) * 32;
  while (top) { ++bits; top >>= 1; }
  return bits;
}

bool BigUint::GetBit(int i) const {
  const size_t limb = static_cast<size_t>(i) / 32;
  if (limb >= limbs_.size()) return false;
  return (limbs_[limb] >> (static_cast<unsigned>(i) % 32)) & 1u;
}

int BigUint::Compare(const BigUint& a, const BigUint& b) {
  if (a.limbs_.size() != b.limbs_.size()) return a.limbs_.size() < b.limbs_.size() ? -1 : 1;
  for (size_t i = a.limbs_.size(); i-- > 0;) {
    if (a.limbs_[i] != b.limbs_[i]) return a.limbs_[i] < b.limbs_[i] ? -1 : 1;
  }
  return 0;
}

BigUint BigUint::Add(const BigUint& a, const BigUint& b) {
  BigUint out;
  const size_t n = std::max(a.limbs_.size(), b.limbs_.size());
  out.limbs_.resize(n, 0);
  std::uint64_t carry = 0;
  for (size_t i = 0; i < n; ++i) {
    const std::uint64_t sum = carry + (i < a.limbs_.size() ? a.limbs_[i] : 0) + (i < b.limbs_.size() ? b.limbs_[i] : 0);
    out.limbs_[i] = static_cast<std::uint32_t>(sum & 0xffffffffu);
    carry = sum >> 32;
  }
  if (carry) out.limbs_.push_back(static_cast<std::uint32_t>(carry));
  out.Trim();
  return out;
}

BigUint BigUint::Sub(const BigUint& a, const BigUint& b) {
  if (Compare(a, b) < 0) throw std::invalid_argument("BigUint::Sub: a < b (unsigned underflow)");
  BigUint out;
  out.limbs_.resize(a.limbs_.size(), 0);
  std::int64_t borrow = 0;
  for (size_t i = 0; i < a.limbs_.size(); ++i) {
    std::int64_t diff = static_cast<std::int64_t>(a.limbs_[i]) - (i < b.limbs_.size() ? static_cast<std::int64_t>(b.limbs_[i]) : 0) - borrow;
    if (diff < 0) { diff += static_cast<std::int64_t>(1) << 32; borrow = 1; } else { borrow = 0; }
    out.limbs_[i] = static_cast<std::uint32_t>(diff);
  }
  out.Trim();
  return out;
}

BigUint BigUint::Mul(const BigUint& a, const BigUint& b) {
  if (a.limbs_.empty() || b.limbs_.empty()) return BigUint();
  BigUint out;
  out.limbs_.assign(a.limbs_.size() + b.limbs_.size(), 0);
  for (size_t i = 0; i < a.limbs_.size(); ++i) {
    std::uint64_t carry = 0;
    const std::uint64_t ai = a.limbs_[i];
    if (ai == 0) continue;
    for (size_t j = 0; j < b.limbs_.size(); ++j) {
      const std::uint64_t prod = ai * static_cast<std::uint64_t>(b.limbs_[j]) + out.limbs_[i + j] + carry;
      out.limbs_[i + j] = static_cast<std::uint32_t>(prod & 0xffffffffu);
      carry = prod >> 32;
    }
    size_t k = i + b.limbs_.size();
    while (carry) {
      const std::uint64_t sum = static_cast<std::uint64_t>(out.limbs_[k]) + carry;
      out.limbs_[k] = static_cast<std::uint32_t>(sum & 0xffffffffu);
      carry = sum >> 32;
      ++k;
    }
  }
  out.Trim();
  return out;
}

void BigUint::DivMod(const BigUint& a, const BigUint& b, BigUint& quotient, BigUint& remainder) {
  if (b.IsZero()) throw std::invalid_argument("BigUint::DivMod: division by zero");
  const int bits = a.BitLength();
  std::vector<std::uint32_t> qlimbs(static_cast<size_t>(bits > 0 ? (bits + 31) / 32 : 0), 0);
  BigUint rem;
  // Plain bit-at-a-time binary long division: shift the running remainder
  // left one bit, bring in the next bit of `a`, and subtract `b` out of it
  // whenever it fits - schoolbook long division, just base-2 instead of
  // base-10, so no multi-word quotient-digit estimation (Knuth's Algorithm
  // D) is needed to get a correct result.
  for (int i = bits - 1; i >= 0; --i) {
    // rem = rem*2 + bit_i(a), done in place on the limb vector directly
    // (rather than via Add/Mul) since this runs `bits` times per division.
    std::uint32_t carry = a.GetBit(i) ? 1u : 0u;
    for (size_t k = 0; k < rem.limbs_.size(); ++k) {
      const std::uint32_t next_carry = rem.limbs_[k] >> 31;
      rem.limbs_[k] = (rem.limbs_[k] << 1) | carry;
      carry = next_carry;
    }
    if (carry) rem.limbs_.push_back(carry);
    if (Compare(rem, b) >= 0) {
      rem = Sub(rem, b);
      qlimbs[static_cast<size_t>(i) / 32] |= (1u << (static_cast<unsigned>(i) % 32));
    }
  }
  quotient.limbs_ = std::move(qlimbs);
  quotient.Trim();
  rem.Trim();
  remainder = std::move(rem);
}

BigUint BigUint::ModPow(const BigUint& base_in, const BigUint& exp, const BigUint& m) {
  if (m.IsZero()) throw std::invalid_argument("BigUint::ModPow: modulus is zero");
  if (m == BigUint(1)) return BigUint();
  BigUint result(1);
  BigUint base = Mod(base_in, m);
  const int bits = exp.BitLength();
  for (int i = 0; i < bits; ++i) {
    if (exp.GetBit(i)) result = Mod(Mul(result, base), m);
    base = Mod(Mul(base, base), m);
  }
  return result;
}

BigUint BigUint::RandomOdd(int bits) {
  if (bits < 2) throw std::invalid_argument("BigUint::RandomOdd: bits must be >= 2");
  BigUint out;
  const size_t n = static_cast<size_t>((bits + 31) / 32);
  out.limbs_.resize(n, 0);
  for (size_t i = 0; i < n; ++i) out.limbs_[i] = SeededWord();
  // Force the top bit of the whole value (so it has exactly `bits` bits,
  // not fewer) and the bottom bit (so it's odd - RSA primes are always odd,
  // and there is no point drawing an even candidate at all).
  const int top_bit_in_limb = (bits - 1) % 32;
  out.limbs_[n - 1] &= (top_bit_in_limb == 31) ? 0xffffffffu : ((1u << (top_bit_in_limb + 1)) - 1u);
  out.limbs_[n - 1] |= (1u << top_bit_in_limb);
  out.limbs_[0] |= 1u;
  out.Trim();
  return out;
}

bool BigUint::IsProbablePrime(const BigUint& n, int rounds) {
  if (n < BigUint(2)) return false;
  for (std::uint32_t p : kSmallPrimes) {
    const BigUint bp(p);
    if (n == bp) return true;
    if (Mod(n, bp).IsZero()) return false;
  }
  // n - 1 = d * 2^s with d odd.
  BigUint d = Sub(n, BigUint(1));
  int s = 0;
  while (d.IsEven()) { BigUint q, r; DivMod(d, BigUint(2), q, r); d = q; ++s; }
  const BigUint n_minus_1 = Sub(n, BigUint(1));
  for (int round = 0; round < rounds; ++round) {
    // A witness base in [2, n-2]; SeededWord()-derived rather than fixed
    // small bases (2,3,5,...) so this also works as a general-purpose
    // probabilistic test, not just a filter tuned to RSA-shaped inputs.
    BigUint a;
    do {
      a = Mod(BigUint::RandomOdd(std::max(2, n.BitLength())), n);
    } while (a < BigUint(2));
    BigUint x = ModPow(a, d, n);
    if (x == BigUint(1) || x == n_minus_1) continue;
    bool composite = true;
    for (int r = 1; r < s; ++r) {
      x = Mod(Mul(x, x), n);
      if (x == n_minus_1) { composite = false; break; }
    }
    if (composite) return false;
  }
  return true;
}

BigUint BigUint::GenerateProbablePrime(int bits) {
  for (;;) {
    BigUint candidate = RandomOdd(bits);
    bool small_factor = false;
    for (std::uint32_t p : kSmallPrimes) {
      if (candidate == BigUint(p)) { small_factor = false; break; }
      if (Mod(candidate, BigUint(p)).IsZero()) { small_factor = true; break; }
    }
    if (small_factor) continue;
    if (IsProbablePrime(candidate)) return candidate;
  }
}

BigInt BigInt::Add(const BigInt& a, const BigInt& b) {
  if (a.negative_ == b.negative_) return BigInt(a.negative_, BigUint::Add(a.mag_, b.mag_));
  if (BigUint::Compare(a.mag_, b.mag_) >= 0) return BigInt(a.negative_, BigUint::Sub(a.mag_, b.mag_));
  return BigInt(b.negative_, BigUint::Sub(b.mag_, a.mag_));
}

BigInt BigInt::Sub(const BigInt& a, const BigInt& b) {
  return Add(a, BigInt(!b.negative_, b.mag_));
}

BigInt BigInt::Mul(const BigInt& a, const BigInt& b) {
  return BigInt(a.negative_ != b.negative_, BigUint::Mul(a.mag_, b.mag_));
}

BigUint BigInt::ModNonNegative(const BigInt& v, const BigUint& m) {
  BigUint r = BigUint::Mod(v.mag_, m);
  if (v.negative_ && !r.IsZero()) r = BigUint::Sub(m, r);
  return r;
}

bool ModInverse(const BigUint& a, const BigUint& m, BigUint& out) {
  // Iterative extended Euclidean algorithm (see Knuth TAOCP vol.2 4.5.2):
  // maintains (old_r, r) as the running gcd remainders and (old_s, s) as
  // the Bezout coefficients of `a`, so old_r == a*old_s + m*(some t) is an
  // invariant throughout - only old_s is tracked since that's the only
  // coefficient ModPow's caller (RSA's d = e^-1 mod phi) actually needs.
  BigUint old_r = a, r = m;
  BigInt old_s = BigInt::FromUnsigned(BigUint(1)), s = BigInt::FromUnsigned(BigUint(0));
  while (!r.IsZero()) {
    BigUint q, rem;
    BigUint::DivMod(old_r, r, q, rem);
    old_r = r;
    r = rem;
    const BigInt q_signed = BigInt::FromUnsigned(q);
    BigInt new_s = BigInt::Sub(old_s, BigInt::Mul(q_signed, s));
    old_s = s;
    s = new_s;
  }
  if (old_r != BigUint(1)) return false;  // gcd(a, m) != 1: no inverse.
  out = BigInt::ModNonNegative(old_s, m);
  return true;
}

}  // namespace dino8::util
