#ifndef EMP_CIRCUIT_CRYPTO_SHA256_H__
#define EMP_CIRCUIT_CRYPTO_SHA256_H__

// SHA-256 (FIPS 180-4) over the BooleanContext value layer. The primitive is the
// word-level compression function; sha256<N> is the full padded hash for a
// compile-time public message length N (bits, a multiple of 8) — N being public
// gives a deterministic circuit. Ch and Maj cost one AND per bit, the Σ/σ
// rotations/shifts are free, and every multi-operand addition mod 2^32 is a
// single column-compressed sum (one AND per full or half adder).
//
// Internally each word bit is either a wire or a public constant known when the
// circuit is built (round constants, the IV, the padding of sha256<N>); gates
// whose value those constants fix are never emitted, and constant addends fold
// into one constant per sum.
//
// I/O is LSB-first within each byte, byte-sequential; the message is read as
// big-endian 32-bit words and the digest emitted big-endian, so feeding bytes and
// reading the 256-bit output reproduces the standard digest.

#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace emp {
namespace circuit {
namespace crypto {

// Round constants K[0..63] and initial hash value H(0)[0..7] (FIPS 180-4).
inline constexpr uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};
inline constexpr uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

template <BooleanContext Ctx> using SHA256Word = UInt_T<Ctx, 32>;

template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_ch(const SHA256Word<Ctx>& x, const SHA256Word<Ctx>& y,
                                 const SHA256Word<Ctx>& z) { return z ^ (x & (y ^ z)); }
// Maj(x,y,z) = x ^ ((x^y) & (x^z)): one AND per bit (x == y gives x, else z).
template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_maj(const SHA256Word<Ctx>& x, const SHA256Word<Ctx>& y,
                                  const SHA256Word<Ctx>& z) { return x ^ ((x ^ y) & (x ^ z)); }
template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_bsig0(const SHA256Word<Ctx>& x) { return x.rotr(2) ^ x.rotr(13) ^ x.rotr(22); }
template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_bsig1(const SHA256Word<Ctx>& x) { return x.rotr(6) ^ x.rotr(11) ^ x.rotr(25); }
template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_ssig0(const SHA256Word<Ctx>& x) { return x.rotr(7) ^ x.rotr(18) ^ (x >> 3); }
template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_ssig1(const SHA256Word<Ctx>& x) { return x.rotr(17) ^ x.rotr(19) ^ (x >> 10); }

namespace detail {

// One bit of an internal SHA-256 word: a wire, or a public constant fixed when
// the circuit is built.
template <BooleanContext Ctx>
struct Sha256Bit {
  typename Ctx::Wire w{};
  int8_t k = -1;   // -1: the value is wire w; 0 or 1: a public constant
};
template <BooleanContext Ctx> using Sha256KWord = std::array<Sha256Bit<Ctx>, 32>;

template <BooleanContext Ctx>
inline Sha256Bit<Ctx> sha256_kbit(bool v) { Sha256Bit<Ctx> b; b.k = v ? 1 : 0; return b; }
template <BooleanContext Ctx>
inline Sha256Bit<Ctx> sha256_wbit(typename Ctx::Wire w) { Sha256Bit<Ctx> b; b.w = w; return b; }

template <BooleanContext Ctx>
inline Sha256Bit<Ctx> sha256_not(Ctx& ctx, Sha256Bit<Ctx> a) {
  if (a.k >= 0) return sha256_kbit<Ctx>(a.k == 0);
  return sha256_wbit<Ctx>(ctx.not_gate(a.w));
}
template <BooleanContext Ctx>
inline Sha256Bit<Ctx> sha256_xor(Ctx& ctx, Sha256Bit<Ctx> a, Sha256Bit<Ctx> b) {
  if (a.k >= 0 && b.k >= 0) return sha256_kbit<Ctx>(a.k != b.k);
  if (a.k >= 0) return a.k ? sha256_not(ctx, b) : b;
  if (b.k >= 0) return b.k ? sha256_not(ctx, a) : a;
  return sha256_wbit<Ctx>(ctx.xor_gate(a.w, b.w));
}
template <BooleanContext Ctx>
inline Sha256Bit<Ctx> sha256_and(Ctx& ctx, Sha256Bit<Ctx> a, Sha256Bit<Ctx> b) {
  if (a.k == 0 || b.k == 0) return sha256_kbit<Ctx>(false);
  if (a.k == 1) return b;
  if (b.k == 1) return a;
  return sha256_wbit<Ctx>(ctx.and_gate(a.w, b.w));
}

template <BooleanContext Ctx>
inline Sha256KWord<Ctx> sha256_kconst(uint32_t v) {
  Sha256KWord<Ctx> r;
  for (int i = 0; i < 32; ++i) r[i] = sha256_kbit<Ctx>((v >> i) & 1);
  return r;
}
template <BooleanContext Ctx>
inline bool sha256_all_known(const Sha256KWord<Ctx>& x) {
  for (const auto& b : x) if (b.k < 0) return false;
  return true;
}
template <BooleanContext Ctx>
inline bool sha256_any_wire(const Sha256KWord<Ctx>& x) { return !sha256_all_known<Ctx>(x); }

// x.rotr(r1) ^ x.rotr(r2) ^ (x.rotr(r3) or x >> r3 when `shift3`): the Σ/σ maps.
template <BooleanContext Ctx>
inline Sha256KWord<Ctx> sha256_ksig(Ctx& ctx, const Sha256KWord<Ctx>& x, int r1, int r2, int r3,
                                    bool shift3) {
  Sha256KWord<Ctx> r;
  for (int i = 0; i < 32; ++i) {
    const Sha256Bit<Ctx> t3 = (shift3 && i + r3 >= 32) ? sha256_kbit<Ctx>(false) : x[(i + r3) % 32];
    r[i] = sha256_xor(ctx, sha256_xor(ctx, x[(i + r1) % 32], x[(i + r2) % 32]), t3);
  }
  return r;
}

template <BooleanContext Ctx>
inline Sha256KWord<Ctx> sha256_kch(Ctx& ctx, const Sha256KWord<Ctx>& e, const Sha256KWord<Ctx>& f,
                                   const Sha256KWord<Ctx>& g) {
  Sha256KWord<Ctx> r;
  for (int i = 0; i < 32; ++i) {
    if (e[i].k >= 0) { r[i] = e[i].k ? f[i] : g[i]; continue; }
    r[i] = sha256_xor(ctx, g[i], sha256_and(ctx, e[i], sha256_xor(ctx, f[i], g[i])));
  }
  return r;
}

template <BooleanContext Ctx>
inline Sha256KWord<Ctx> sha256_kmaj(Ctx& ctx, const Sha256KWord<Ctx>& a, const Sha256KWord<Ctx>& b,
                                    const Sha256KWord<Ctx>& c) {
  Sha256KWord<Ctx> r;
  for (int i = 0; i < 32; ++i) {
    const auto &x = a[i], &y = b[i], &z = c[i];
    if (x.k >= 0 && y.k >= 0) { r[i] = (x.k == y.k) ? x : z; continue; }
    if (x.k >= 0 && z.k >= 0) { r[i] = (x.k == z.k) ? x : y; continue; }
    if (y.k >= 0 && z.k >= 0) { r[i] = (y.k == z.k) ? y : x; continue; }
    r[i] = sha256_xor(ctx, x, sha256_and(ctx, sha256_xor(ctx, x, y), sha256_xor(ctx, x, z)));
  }
  return r;
}

// Sum of the words plus the public constant `c`, mod 2^32, by column
// compression. Known bits fold into `c`. Column i's h wire bits are reduced
// with full adders (one AND each, carry to column i+1) to one or two bits; two
// left take a half adder (one AND), one left passes through. A set bit of `c` is
// absorbed for free when one bit is left (x + 1 = ~x + 2x) and as a full adder
// with a constant input when two are left (sum ~(x^y), carry x|y). So column i
// costs floor(h/2) ANDs and the top column, whose carries are dropped, none: a
// sum of k wire words costs at most the 31(k-1) ANDs of k-1 chained ripple
// adders, and saves in the low columns.
template <BooleanContext Ctx>
inline Sha256KWord<Ctx> sha256_ksum(Ctx& ctx, std::initializer_list<const Sha256KWord<Ctx>*> terms,
                                    uint32_t c) {
  using W = typename Ctx::Wire;
  std::array<std::vector<W>, 32> col;
  for (const Sha256KWord<Ctx>* t : terms)
    for (int i = 0; i < 32; ++i) {
      const Sha256Bit<Ctx>& b = (*t)[i];
      if (b.k >= 0) c += static_cast<uint32_t>(b.k) << i;
      else col[i].push_back(b.w);
    }
  Sha256KWord<Ctx> r;
  for (int i = 0; i < 32; ++i) {
    std::vector<W>& v = col[i];
    const bool k = (c >> i) & 1;
    if (i == 31) {
      Sha256Bit<Ctx> s = sha256_kbit<Ctx>(k);
      for (W x : v) s = sha256_xor(ctx, s, sha256_wbit<Ctx>(x));
      r[i] = s;
      break;
    }
    std::size_t p = 0;
    while (v.size() - p >= 3) {
      const W x = v[p], y = v[p + 1], z = v[p + 2];
      p += 3;
      const W xy = ctx.xor_gate(x, y);
      v.push_back(ctx.xor_gate(xy, z));
      col[i + 1].push_back(ctx.xor_gate(x, ctx.and_gate(xy, ctx.xor_gate(x, z))));
    }
    const std::size_t left = v.size() - p;
    if (left == 0) {
      r[i] = sha256_kbit<Ctx>(k);
    } else if (left == 1) {
      const W x = v[p];
      if (!k) {
        r[i] = sha256_wbit<Ctx>(x);
      } else {
        r[i] = sha256_wbit<Ctx>(ctx.not_gate(x));
        col[i + 1].push_back(x);
      }
    } else {
      const W x = v[p], y = v[p + 1];
      const W xy = ctx.xor_gate(x, y), a = ctx.and_gate(x, y);
      if (!k) {
        r[i] = sha256_wbit<Ctx>(xy);
        col[i + 1].push_back(a);
      } else {
        r[i] = sha256_wbit<Ctx>(ctx.not_gate(xy));
        col[i + 1].push_back(ctx.xor_gate(xy, a));
      }
    }
  }
  return r;
}

// Compression over internal words: state += compress(state, block).
template <BooleanContext Ctx>
inline void sha256_compress_k(Ctx& ctx, Sha256KWord<Ctx> state[8], const Sha256KWord<Ctx> block[16]) {
  using KW = Sha256KWord<Ctx>;
  std::vector<KW> W(64);
  for (int j = 0; j < 16; ++j) W[j] = block[j];
  for (int t = 16; t < 64; ++t) {
    const KW s1 = sha256_ksig(ctx, W[t - 2], 17, 19, 10, true);
    const KW s0 = sha256_ksig(ctx, W[t - 15], 7, 18, 3, true);
    W[t] = sha256_ksum(ctx, {&s1, &W[t - 7], &s0, &W[t - 16]}, 0);
  }

  KW a = state[0], b = state[1], c = state[2], d = state[3];
  KW e = state[4], f = state[5], g = state[6], h = state[7];
  for (int t = 0; t < 64; ++t) {
    const KW S1 = sha256_ksig(ctx, e, 6, 11, 25, false);
    const KW CH = sha256_kch(ctx, e, f, g);
    const KW S0 = sha256_ksig(ctx, a, 2, 13, 22, false);
    const KW MJ = sha256_kmaj(ctx, a, b, c);
    // T1 = h + Σ1 + Ch + K + W feeds e' = d + T1 and a' = T1 + Σ0 + Maj. When T1
    // has one wire term and d, Σ0 and Maj are public (the first round after a
    // public IV), adding that term into e' and a' directly beats summing T1 first.
    const int t1_wires = sha256_any_wire<Ctx>(h) + sha256_any_wire<Ctx>(S1) +
                         sha256_any_wire<Ctx>(CH) + sha256_any_wire<Ctx>(W[t]);
    KW e_new, a_new;
    if (t1_wires <= 1 && sha256_all_known<Ctx>(d) && sha256_all_known<Ctx>(S0) &&
        sha256_all_known<Ctx>(MJ)) {
      e_new = sha256_ksum(ctx, {&h, &W[t], &S1, &CH, &d}, SHA256_K[t]);
      a_new = sha256_ksum(ctx, {&h, &W[t], &S1, &CH, &S0, &MJ}, SHA256_K[t]);
    } else {
      const KW T1 = sha256_ksum(ctx, {&h, &W[t], &S1, &CH}, SHA256_K[t]);
      e_new = sha256_ksum(ctx, {&d, &T1}, 0);
      a_new = sha256_ksum(ctx, {&S0, &MJ, &T1}, 0);
    }
    h = g; g = f; f = e; e = e_new;
    d = c; c = b; b = a; a = a_new;
  }
  const KW* v[8] = {&a, &b, &c, &d, &e, &f, &g, &h};
  for (int i = 0; i < 8; ++i) state[i] = sha256_ksum(ctx, {&state[i], v[i]}, 0);
}

template <BooleanContext Ctx>
inline Sha256KWord<Ctx> sha256_kword(const UInt_T<Ctx, 32>& x) {
  Sha256KWord<Ctx> r;
  for (int i = 0; i < 32; ++i) r[i] = sha256_wbit<Ctx>(x.w[(std::size_t)i]);
  return r;
}
template <BooleanContext Ctx>
inline typename Ctx::Wire sha256_bit_wire(Ctx& ctx, const Sha256Bit<Ctx>& b) {
  return b.k >= 0 ? ctx.public_bit(b.k == 1) : b.w;
}

}  // namespace detail

// One compression step: fold a 16-word message block into the 8-word state.
// state is updated in place (state[i] += compressed working variable).
template <BooleanContext Ctx>
inline void sha256_compress(Ctx& ctx, UInt_T<Ctx, 32> state[8],
                            const UInt_T<Ctx, 32> block[16]) {
  detail::Sha256KWord<Ctx> s[8], m[16];
  for (int i = 0; i < 8; ++i) s[i] = detail::sha256_kword<Ctx>(state[i]);
  for (int j = 0; j < 16; ++j) m[j] = detail::sha256_kword<Ctx>(block[j]);
  detail::sha256_compress_k<Ctx>(ctx, s, m);
  for (int i = 0; i < 8; ++i)
    for (int b = 0; b < 32; ++b) state[i].w[(std::size_t)b] = detail::sha256_bit_wire(ctx, s[i][b]);
}

// Big-endian word `word`, bit `b` -> flat bit index within a 64-byte block
// (LSB-first within byte). Used for both reading message words and writing the digest.
inline constexpr std::size_t sha256_word_bit_index(std::size_t word, std::size_t b) {
  return 8 * (4 * word + 3 - b / 8) + (b % 8);
}

namespace detail {

// Full SHA-256 of a fixed public N-bit message (N a multiple of 8). Padding and
// IV are public, so the circuit shape is fixed per N and their gates fold away.
// `in` points at the N message wires. Writes 256 output wires.
template <BooleanContext Ctx, int N>
inline void sha256_wires(Ctx& ctx, typename Ctx::Wire out[256], const typename Ctx::Wire* in) {
  static_assert(N % 8 == 0, "sha256<N>: byte-granular message length only");
  static_assert(N >= 0, "sha256<N>: bit length must be non-negative");
  using B = Sha256Bit<Ctx>;
  const std::size_t L = static_cast<std::size_t>(N);
  const std::size_t total = ((L + 1 + 64 + 511) / 512) * 512;

  // Padded message, flat LSB-first: data bits are wires, pad bits are public.
  std::vector<B> P(total, sha256_kbit<Ctx>(false));
  for (std::size_t i = 0; i < L; ++i) P[i] = sha256_wbit<Ctx>(in[i]);
  P[L + 7] = sha256_kbit<Ctx>(true);                 // '1' bit = MSB of the 0x80 pad byte
  const std::size_t lenpos = total - 64;             // 64-bit big-endian length
  for (std::size_t t = 0; t < 8; ++t) {
    const uint64_t byte = (static_cast<uint64_t>(L) >> (8 * (7 - t))) & 0xff;
    for (std::size_t k = 0; k < 8; ++k)
      if ((byte >> k) & 1) P[lenpos + 8 * t + k] = sha256_kbit<Ctx>(true);
  }

  Sha256KWord<Ctx> state[8];
  for (int i = 0; i < 8; ++i) state[i] = sha256_kconst<Ctx>(SHA256_H0[i]);

  const std::size_t nblocks = total / 512;
  for (std::size_t blk = 0; blk < nblocks; ++blk) {
    Sha256KWord<Ctx> msg[16];
    for (int j = 0; j < 16; ++j)
      for (int b = 0; b < 32; ++b) msg[j][b] = P[blk * 512 + sha256_word_bit_index(j, b)];
    sha256_compress_k<Ctx>(ctx, state, msg);
  }

  for (int i = 0; i < 8; ++i)
    for (int b = 0; b < 32; ++b)
      out[sha256_word_bit_index(i, b)] = sha256_bit_wire(ctx, state[i][b]);
}

}  // namespace detail

template <BooleanContext Ctx, int N>
inline BitVec_T<Ctx, 256> sha256(Ctx& ctx, const BitVec_T<Ctx, N>& in) {
  BitVec_T<Ctx, 256> out(ctx);
  detail::sha256_wires<Ctx, N>(ctx, out.w.data(), in.w.data());
  return out;
}

}  // namespace crypto
}  // namespace circuit
}  // namespace emp
#endif  // EMP_CIRCUIT_CRYPTO_SHA256_H__
