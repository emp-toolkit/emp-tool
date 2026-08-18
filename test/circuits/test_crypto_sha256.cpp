// SHA-256 (FIPS 180-4) over the BooleanContext value layer, example-driven on a
// ClearSession (the I/O boundary feeds inputs and reveals results; the values
// themselves are pure context-bound circuit values). Sections:
//   - example(): hash a short BitVec message ("abc"), reveal 256 bits, compare
//     to the known digest ba7816bf...
//   - named NIST vectors for "" and "abc"
//   - OpenSSL cross-check (SHA256()) at padding-boundary lengths around the
//     55/56-byte single-block edge and the 64-byte block
//   - a direct sha256_compress on the padded "abc" block, from the IV
//   - record/replay equivalence and the AND count (24744) of the fixed
//     sha256(BitVec<256>) wrapper, via a tiny low-level IR driver
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include "emp-tool/circuits/crypto/sha256.h"
#include "test_crypto_common.h"
#include <openssl/sha.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <type_traits>
#include <vector>
using namespace emp;
using namespace emp::circuit::crypto;
using namespace test_crypto;
using Ctx = ClearSession::ctx_t;

using UInt32 = UInt_T<Ctx, 32>;
using BV256  = BitVec_T<Ctx, 256>;

static int g_fail = 0;
static void check(const char* name, bool ok) {
  if (!ok) { printf("  [FAIL] %s\n", name); ++g_fail; }
}

// --- bit/byte helpers (LSB-first within byte, byte-sequential) ---------------

// Clear digest bytes (32) from a revealed 256-bit BitVec value.
static std::array<uint8_t, 32> bytes_from_digest(const std::array<bool, 256>& bits) {
  std::array<uint8_t, 32> out{};
  for (int i = 0; i < 32; ++i)
    for (int k = 0; k < 8; ++k)
      out[i] |= (uint8_t)(bits[i * 8 + k] & 1) << k;
  return out;
}
static std::string hex32(const std::array<uint8_t, 32>& d) {
  static const char* H = "0123456789abcdef";
  std::string s;
  for (uint8_t b : d) { s += H[b >> 4]; s += H[b & 15]; }
  return s;
}

// Build the std::array<bool,N> clear message (LSB-first per byte) from N/8 bytes.
template <int N>
static std::array<bool, N> message_clear(const uint8_t* bytes) {
  static_assert(N % 8 == 0, "byte-granular message");
  std::array<bool, N> v{};
  for (int i = 0; i < N / 8; ++i)
    for (int k = 0; k < 8; ++k)
      v[i * 8 + k] = ((bytes[i] >> k) & 1) != 0;
  return v;
}

// Feed an N-bit message as a party input through the session.
template <int N>
static BitVec_T<Ctx, N> message_bits(ClearSession& sess, const uint8_t* bytes) {
  if constexpr (N == 0) {
    return BitVec_T<Ctx, 0>(sess.ctx());
  } else {
    return sess.input<BitVec_T<Ctx, N>>(ALICE, message_clear<N>(bytes));
  }
}

// In-circuit SHA-256 of a compile-time N-bit message, returned as 32 bytes.
template <int N>
static std::array<uint8_t, 32> circuit_sha256(const uint8_t* bytes) {
  ClearSession sess;
  BitVec_T<Ctx, N> msg = message_bits<N>(sess, bytes);
  BV256 dig = sha256(sess.ctx(), msg);
  return bytes_from_digest(sess.reveal(dig, PUBLIC).value());
}

// OpenSSL reference digest.
static std::array<uint8_t, 32> openssl_sha256(const uint8_t* in, size_t n) {
  std::array<uint8_t, 32> out{};
  SHA256(in, n, out.data());
  return out;
}

// =============================================================================
// example(): the readable user-facing entry. Feed "abc" as a 24-bit message
// owned by ALICE, hash it, reveal the 256-bit digest, and compare to the
// FIPS-180-4 "abc" vector.
static void example() {
  ClearSession sess;
  const uint8_t abc[3] = {'a', 'b', 'c'};
  auto msg = sess.input<BitVec_T<Ctx, 24>>(ALICE, message_clear<24>(abc));

  BV256 digest = sha256(sess.ctx(), msg);
  std::string got = hex32(bytes_from_digest(sess.reveal(digest, PUBLIC).value()));

  const std::string want =
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  check("example SHA-256(\"abc\")", got == want);
  if (got != want) printf("    got %s\n    want %s\n", got.c_str(), want.c_str());
}

// =============================================================================
// Named NIST vectors.
static void section_vectors() {
  {
    const uint8_t abc[3] = {'a', 'b', 'c'};
    std::string got = hex32(circuit_sha256<24>(abc));
    const std::string want =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    check("vector abc", got == want);
    if (got != want) printf("    got %s want %s\n", got.c_str(), want.c_str());
  }
  {
    std::string got = hex32(circuit_sha256<0>(nullptr));
    const std::string want =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    check("vector empty", got == want);
    if (got != want) printf("    got %s want %s\n", got.c_str(), want.c_str());
  }
}

// =============================================================================
// OpenSSL cross-check at padding-boundary lengths. The byte length determines
// how many compression blocks the padding spans: 0..55 fit one block, 56..63
// spill the length field into a second block, 64 starts a fresh block, etc.
// Deterministic seeded data (no time-based seed).
template <int Bytes>
static void xcheck_len(std::mt19937_64& rng) {
  std::array<uint8_t, (Bytes == 0 ? 1 : Bytes)> buf{};
  for (int i = 0; i < Bytes; ++i) buf[i] = (uint8_t)(rng() & 0xff);
  std::array<uint8_t, 32> got = circuit_sha256<Bytes * 8>(buf.data());
  std::array<uint8_t, 32> ref = openssl_sha256(buf.data(), (size_t)Bytes);
  bool ok = (got == ref);
  char name[48];
  std::snprintf(name, sizeof(name), "xcheck len=%d", Bytes);
  check(name, ok);
  if (!ok) printf("    got %s\n    ref %s\n", hex32(got).c_str(), hex32(ref).c_str());
}
static void section_openssl_xcheck() {
  std::mt19937_64 rng(0x5A256ULL);
  xcheck_len<0>(rng);    xcheck_len<1>(rng);   xcheck_len<55>(rng);
  xcheck_len<56>(rng);   xcheck_len<57>(rng);  xcheck_len<64>(rng);
  xcheck_len<119>(rng);  xcheck_len<120>(rng);
}

// =============================================================================
// Direct sha256_compress check: build the single padded 512-bit block for
// "abc" as 16 big-endian 32-bit words, fold it into the IV with one compress
// call, and confirm the resulting state words are the SHA-256("abc") digest.
static void section_compress() {
  ClearSession sess;
  Ctx& ctx = sess.ctx();   // raw ctx for public-constant word construction

  // "abc" = 0x616263; pad: append 0x80, zeros, then the 64-bit bit-length 0x18.
  const uint32_t blk[16] = {0x61626380, 0, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 0x18};
  UInt32 block[16];
  for (int j = 0; j < 16; ++j) block[j] = UInt32::constant(ctx, blk[j]);

  UInt32 state[8];
  for (int i = 0; i < 8; ++i) state[i] = UInt32::constant(ctx, SHA256_H0[i]);

  sha256_compress(ctx, state, block);

  const uint32_t want[8] = {0xba7816bf, 0x8f01cfea, 0x414140de, 0x5dae2223,
                            0xb00361a3, 0x96177a9c, 0xb410ff61, 0xf20015ad};
  bool ok = true;
  for (int i = 0; i < 8; ++i) {
    uint32_t w = sess.reveal<uint32_t>(state[i], PUBLIC).value();
    if (w != want[i]) { ok = false; printf("    word %d got %08x want %08x\n", i, w, want[i]); }
  }
  check("direct sha256_compress(\"abc\")", ok);
}

// =============================================================================
// Low-level IR plumbing (not a user-facing example): record/replay equivalence
// + AND count of the fixed sha256(BitVec<256>) wrapper. The driver is generic
// over the analysis contexts and is the only place that touches raw wires.
static void section_ir() {
  // in = 256 message wires (one padded block); out = 256 digest wires.
  auto drv256 = [](auto& ctx, auto in, auto out) {
    using Ctx = std::decay_t<decltype(ctx)>;
    using Msg = BitVec_T<Ctx, 256>;
    Msg msg = Msg::from_wires(ctx, in);
    auto dig = sha256(ctx, msg);
    dig.pack_wires(out);
  };

  std::vector<uint8_t> msg256(256);
  std::mt19937_64 rng(0xABCDEF1234567890ULL);
  for (int i = 0; i < 256; ++i) msg256[i] = (uint8_t)(rng() & 1);

  std::vector<uint8_t> live = clear_run(drv256, msg256, 256);
  std::vector<uint8_t> rep  = record_replay(drv256, msg256, 256);
  uint64_t ands = count_ands(drv256, 256, 256);

  check("record == replay", rep == live);
  bool cnt = (ands == 24744);
  check("AND count == 24744", cnt);
  if (!cnt) printf("    ands=%llu (want 24744)\n", (unsigned long long)ands);
}

int main() {
  example();
  section_vectors();
  section_openssl_xcheck();
  section_compress();
  section_ir();

  if (g_fail == 0) printf("test_crypto_sha256: PASS\n");
  else             printf("test_crypto_sha256: FAILED (%d)\n", g_fail);
  return g_fail ? 1 : 0;
}
