// AES-128 over the BooleanContext value layer (emp::circuit::crypto), exercised on
// Ctx in the readable BitVec session style:
//   - example():    encrypt a 128-bit block under a key and reveal it
//   - aes_sbox:     all 256 input bytes vs the published AES S-box table
//   - FIPS-197:     the Appendix B / C.1 known-answer vector
//   - OpenSSL XV:   deterministic random key/pt pairs vs AES_encrypt (libcrypto)
//   - AES-128-CTR:  counter increment + carry, two-block and multi-block keystream,
//                   start_chunk offset, all cross-checked vs EVP_aes_128_ctr
//   - IR plumbing:  record/replay equivalence + AND counts (aes_sbox==28, block==5600)
//
// Inputs are fed and results revealed through a ClearSession — the I/O boundary;
// the values themselves are pure context-bound circuit values, and crypto kernels
// take the raw ctx (sess.ctx()) as their first argument.
//
// Bit/byte convention throughout: LSB-first within each byte, bytes in natural
// order. The BitVec clear_t is std::array<bool,N> in that same layout, so
// bits_of<N>() feeds straight into sess.input<...> and sess.reveal returns it.
#include "emp-tool/circuits/crypto/aes128.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include "test_crypto_common.h"
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <random>
#include <type_traits>
#include <vector>

using namespace emp;
using namespace emp::circuit::crypto;
using namespace test_crypto;
using Ctx = ClearSession::ctx_t;

using Byte  = BitVec_T<Ctx, 8>;
using Block = BitVec_T<Ctx, 128>;

// ---- local check helpers (no global mutable state besides the failure tally) ----
static int g_fail = 0;
static void check(const char* name, bool ok) {
  if (!ok) { printf("  [FAIL] %s\n", name); ++g_fail; }
}
static void check_hex(const char* name, const std::string& got, const std::string& want) {
  if (got != want) {
    printf("  [FAIL] %s: got %s want %s\n", name, got.c_str(), want.c_str());
    ++g_fail;
  }
}

// ---- readable conversions between bytes and BitVec clear values ----
template <int N>
static std::array<bool, N> bits_of(const unsigned char* bytes) {  // LSB-first within byte
  static_assert(N % 8 == 0, "bits_of: N must be a whole number of bytes");
  std::array<bool, N> a{};
  for (int i = 0; i < N; ++i) a[i] = ((bytes[i / 8] >> (i % 8)) & 1) != 0;
  return a;
}
template <int N>
static std::array<bool, N> bits_of(const std::vector<uint8_t>& bit_bools) {  // already LSB-first bits
  std::array<bool, N> a{};
  for (int i = 0; i < N && i < (int)bit_bools.size(); ++i) a[i] = bit_bools[i] & 1;
  return a;
}
template <int N>
static void bytes_of(const std::array<bool, N>& a, unsigned char* out) {  // LSB-first within byte
  static_assert(N % 8 == 0, "bytes_of: N must be a whole number of bytes");
  for (int i = 0; i < N / 8; ++i) {
    unsigned char b = 0;
    for (int k = 0; k < 8; ++k) b |= (unsigned char)(a[i * 8 + k] & 1) << k;
    out[i] = b;
  }
}
static std::string hex_of_bytes(const unsigned char* bytes, int n) {
  static const char* H = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < n; ++i) { s += H[bytes[i] >> 4]; s += H[bytes[i] & 15]; }
  return s;
}
// hex string -> 16 packed bytes (for the OpenSSL key/IV byte buffers).
static void bytes_from_hex(const std::string& hex, unsigned char* out, int n) {
  auto bits = bits_from_hex(hex);
  for (int i = 0; i < n; ++i) {
    unsigned char b = 0;
    for (int k = 0; k < 8; ++k) b |= (unsigned char)(bits[i * 8 + k] & 1) << k;
    out[i] = b;
  }
}

// ---------------------------------------------------------------------------
// 1. example(): how a normal user feeds inputs, encrypts a block, and reveals.
// ---------------------------------------------------------------------------
static void example() {
  ClearSession sess;

  // FIPS-197 C.1 known answer: AES-128(key, pt) for the standard test vectors.
  auto key = sess.input<Block>(ALICE, bits_of<128>(bits_from_hex("000102030405060708090a0b0c0d0e0f")));
  auto pt  = sess.input<Block>(BOB,   bits_of<128>(bits_from_hex("00112233445566778899aabbccddeeff")));

  Block ct = aes128_encrypt(sess.ctx(), pt, key);

  unsigned char out[16];
  bytes_of<128>(sess.reveal(ct, PUBLIC).value(), out);
  check_hex("example AES-128 block", hex_of_bytes(out, 16),
            "69c4e0d86a7b0430d8cdb78070b4c55a");
}

// ---------------------------------------------------------------------------
// 2. aes_sbox over all 256 input bytes vs the published AES S-box table.
// ---------------------------------------------------------------------------
static const unsigned char AES_SBOX_TABLE[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static void test_sbox() {
  ClearSession sess;
  int bad = 0;
  for (int v = 0; v < 256; ++v) {
    unsigned char vb = (unsigned char)v;
    auto in = sess.input<Byte>(ALICE, bits_of<8>(&vb));
    Byte out = aes_sbox(sess.ctx(), in);
    unsigned char got;
    bytes_of<8>(sess.reveal(out, PUBLIC).value(), &got);
    if (got != AES_SBOX_TABLE[v]) {
      if (bad < 5)
        printf("  [FAIL] aes_sbox v=%02x got=%02x want=%02x\n", v, got, AES_SBOX_TABLE[v]);
      ++bad;
    }
  }
  check("aes_sbox over all 256 inputs", bad == 0);
}

// ---------------------------------------------------------------------------
// 3. FIPS-197 Appendix B vector (worked example: 3243f6a8... under 2b7e1516...).
// ---------------------------------------------------------------------------
static void test_fips197() {
  ClearSession sess;

  // Appendix B: the textbook worked example.
  auto pt  = sess.input<Block>(ALICE, bits_of<128>(bits_from_hex("3243f6a8885a308d313198a2e0370734")));
  auto key = sess.input<Block>(ALICE, bits_of<128>(bits_from_hex("2b7e151628aed2a6abf7158809cf4f3c")));
  unsigned char out[16];
  bytes_of<128>(sess.reveal(aes128_encrypt(sess.ctx(), pt, key), PUBLIC).value(), out);
  check_hex("FIPS-197 Appendix B", hex_of_bytes(out, 16),
            "3925841d02dc09fbdc118597196a0b32");

  // Also exercise the two-stage form: key_schedule then encrypt_block.
  using Exp = BitVec_T<Ctx, 1408>;
  Exp exp = aes128_key_schedule(sess.ctx(), key);
  Block ct2 = aes128_encrypt_block(sess.ctx(), pt, exp);
  bytes_of<128>(sess.reveal(ct2, PUBLIC).value(), out);
  check_hex("FIPS-197 via key_schedule+encrypt_block", hex_of_bytes(out, 16),
            "3925841d02dc09fbdc118597196a0b32");
}

// ---------------------------------------------------------------------------
// 4. Deterministic random cross-check vs OpenSSL AES_encrypt (one block, ECB).
// ---------------------------------------------------------------------------
static void test_openssl_block() {
  ClearSession sess;
  std::mt19937_64 rng(0xA5C0FFEEull);   // fixed seed: reproducible, no Date/time
  int bad = 0;
  const int trials = 64;
  for (int t = 0; t < trials; ++t) {
    unsigned char k[16], p[16];
    for (int i = 0; i < 16; ++i) { k[i] = (unsigned char)(rng() & 0xff); p[i] = (unsigned char)(rng() & 0xff); }

    // AES_set_encrypt_key/AES_encrypt are the low-level libcrypto reference path.
    unsigned char ref[16];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    AES_KEY ek;
    AES_set_encrypt_key(k, 128, &ek);
    AES_encrypt(p, ref, &ek);
#pragma clang diagnostic pop

    auto key = sess.input<Block>(ALICE, bits_of<128>(k));
    auto pt  = sess.input<Block>(BOB,   bits_of<128>(p));
    unsigned char mine[16];
    bytes_of<128>(sess.reveal(aes128_encrypt(sess.ctx(), pt, key), PUBLIC).value(), mine);

    if (memcmp(mine, ref, 16) != 0) {
      if (bad < 3)
        printf("  [FAIL] openssl block trial %d: mine=%s ref=%s\n", t,
               hex_of_bytes(mine, 16).c_str(), hex_of_bytes(ref, 16).c_str());
      ++bad;
    }
  }
  check("AES block vs OpenSSL AES_encrypt (random)", bad == 0);
}

// OpenSSL EVP AES-128-CTR ciphertext for cross-checking aes128_ctr. The 16-byte iv
// is the initial counter block (big-endian counter in bytes 8..15); start_chunk
// offsets that counter, matching aes128_ctr's start_chunk argument.
static std::vector<unsigned char> openssl_ctr(const unsigned char key[16],
                                              const unsigned char iv16[16],
                                              const std::vector<unsigned char>& in,
                                              uint64_t start_chunk) {
  unsigned char ivc[16];
  memcpy(ivc, iv16, 16);
  uint64_t carry = start_chunk;
  for (int byte = 15; byte >= 8 && carry; --byte) {
    uint64_t s = (uint64_t)ivc[byte] + (carry & 0xff);
    ivc[byte] = (unsigned char)(s & 0xff);
    carry = (carry >> 8) + (s >> 8);
  }
  std::vector<unsigned char> out(in.size());
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  EVP_EncryptInit_ex(c, EVP_aes_128_ctr(), nullptr, key, ivc);
  int outl = 0;
  EVP_EncryptUpdate(c, out.data(), &outl, in.data(), (int)in.size());
  EVP_CIPHER_CTX_free(c);
  return out;
}

// ---------------------------------------------------------------------------
// 5. AES-128-CTR: counter increment + carry, multi-block keystream, start_chunk,
//    all cross-checked against OpenSSL EVP_aes_128_ctr.
// ---------------------------------------------------------------------------
static void test_ctr() {
  ClearSession sess;

  // (a) ctr_inc_be64 by 1 on a zero block -> byte 15 (BE LSB) == 0x01.
  Block zero = Block::constant(sess.ctx(), std::array<bool, 128>{});
  unsigned char z1[16];
  bytes_of<128>(sess.reveal(ctr_inc_be64(sess.ctx(), zero, 1), PUBLIC).value(), z1);
  check_hex("ctr_inc +1", hex_of_bytes(z1, 16), "00000000000000000000000000000001");

  // (b) carry across a byte boundary: byte 15 == 0xff, +1 -> 00, byte 14 -> 01.
  unsigned char ffb[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xff};
  auto cff = sess.input<Block>(ALICE, bits_of<128>(ffb));
  unsigned char c1[16];
  bytes_of<128>(sess.reveal(ctr_inc_be64(sess.ctx(), cff, 1), PUBLIC).value(), c1);
  check_hex("ctr_inc carry", hex_of_bytes(c1, 16), "00000000000000000000000000000100");

  // (c) a large delta that ripples several bytes (byte 8..15 only; bytes 0..7 stay).
  unsigned char base[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                            0,0,0,0,0,0,0xff,0xff};
  auto cb = sess.input<Block>(ALICE, bits_of<128>(base));
  unsigned char cd[16];
  bytes_of<128>(sess.reveal(ctr_inc_be64(sess.ctx(), cb, 1), PUBLIC).value(), cd);
  check_hex("ctr_inc multi-byte carry", hex_of_bytes(cd, 16),
            "11223344556677880000000000010000");

  // The standard NIST SP 800-38A CTR key/IV.
  unsigned char keyb[16], ivb[16];
  bytes_from_hex("2b7e151628aed2a6abf7158809cf4f3c", keyb, 16);
  bytes_from_hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", ivb, 16);
  auto key = sess.input<Block>(ALICE, bits_of<128>(keyb));
  auto iv  = sess.input<Block>(ALICE, bits_of<128>(ivb));

  // (d) Two-block keystream: ct == pt ^ AES(counter_i), counter_1 = inc(counter_0).
  {
    using Two = BitVec_T<Ctx, 256>;
    const std::string pth = "6bc1bee22e409f96e93d7e117393172a"
                            "ae2d8a571e03ac9c9eb76fac45af8e51";
    auto ptbits = bits_of<256>(bits_from_hex(pth));
    auto pt = sess.input<Two>(BOB, ptbits);
    Two ct = aes128_ctr(sess.ctx(), key, iv, pt);

    // Reconstruct the keystream from the block primitive.
    Block ctr1 = ctr_inc_be64(sess.ctx(), iv, 1);
    Block ks0 = aes128_encrypt(sess.ctx(), iv, key);
    Block ks1 = aes128_encrypt(sess.ctx(), ctr1, key);
    auto ctb = sess.reveal(ct, PUBLIC).value();
    auto ks0b = sess.reveal(ks0, PUBLIC).value();
    auto ks1b = sess.reveal(ks1, PUBLIC).value();
    bool loop_ok = true;
    for (int i = 0; i < 128; ++i) loop_ok = loop_ok && ((bool)ctb[i]       == (ptbits[i]       ^ (bool)ks0b[i]));
    for (int i = 0; i < 128; ++i) loop_ok = loop_ok && ((bool)ctb[128 + i] == (ptbits[128 + i] ^ (bool)ks1b[i]));
    check("CTR two-block == pt ^ keystream", loop_ok);

    // Cross-check the same two blocks against OpenSSL.
    std::vector<unsigned char> inb(32);
    bytes_of<256>(ptbits, inb.data());
    std::vector<unsigned char> ref = openssl_ctr(keyb, ivb, inb, 0);
    unsigned char mine[32]; bytes_of<256>(ctb, mine);
    check("CTR two-block vs OpenSSL", memcmp(mine, ref.data(), 32) == 0);
  }

  // (e) Longer multi-block message (5 full blocks = 640 bits) vs OpenSSL.
  {
    constexpr int NB = 5, N = NB * 128;
    using Long = BitVec_T<Ctx, N>;
    std::mt19937_64 rng(0x1234abcdull);
    std::vector<unsigned char> inb(NB * 16);
    for (auto& x : inb) x = (unsigned char)(rng() & 0xff);
    auto pt = sess.input<Long>(BOB, bits_of<N>(inb.data()));
    unsigned char mine[NB * 16];
    bytes_of<N>(sess.reveal(aes128_ctr(sess.ctx(), key, iv, pt), PUBLIC).value(), mine);
    std::vector<unsigned char> ref = openssl_ctr(keyb, ivb, inb, 0);
    check("CTR 5-block vs OpenSSL", memcmp(mine, ref.data(), NB * 16) == 0);
  }

  // (f) start_chunk offset: encrypting block k of a stream uses counter_0 + k.
  {
    constexpr int NB = 3, N = NB * 128;
    using Long = BitVec_T<Ctx, N>;
    std::mt19937_64 rng(0xfeedface77ull);
    std::vector<unsigned char> inb(NB * 16);
    for (auto& x : inb) x = (unsigned char)(rng() & 0xff);
    auto pt = sess.input<Long>(BOB, bits_of<N>(inb.data()));
    for (uint64_t start : {uint64_t(1), uint64_t(7), uint64_t(255), uint64_t(256), uint64_t(1000)}) {
      unsigned char mine[NB * 16];
      bytes_of<N>(sess.reveal(aes128_ctr(sess.ctx(), key, iv, pt, start), PUBLIC).value(), mine);
      std::vector<unsigned char> ref = openssl_ctr(keyb, ivb, inb, start);
      char nm[64]; snprintf(nm, sizeof nm, "CTR start_chunk=%llu vs OpenSSL", (unsigned long long)start);
      check(nm, memcmp(mine, ref.data(), NB * 16) == 0);
    }
  }

  // (g) round-trip: CTR is its own inverse (encrypt then decrypt == identity).
  {
    constexpr int N = 256;
    using Two = BitVec_T<Ctx, N>;
    std::mt19937_64 rng(0xdeadbeef01ull);
    std::array<bool, N> inbits{};
    for (int i = 0; i < N; ++i) inbits[i] = rng() & 1;
    auto pt = sess.input<Two>(BOB, inbits);
    Two ct = aes128_ctr(sess.ctx(), key, iv, pt, 42);
    Two rt = aes128_ctr(sess.ctx(), key, iv, ct, 42);
    check("CTR encrypt/decrypt round-trip", sess.reveal(rt, PUBLIC).value() == inbits);
  }
}

// ---------------------------------------------------------------------------
// 6. IR plumbing: record/replay equivalence + AND-gate counts.
//    Tiny wire-level drivers are the legitimate place to touch raw wires; this
//    section is deliberately low-level (not a user-facing example).
// ---------------------------------------------------------------------------
static void test_ir_plumbing() {
  // aes_sbox driver: 8 input wires -> 8 output wires.
  auto drv_sbox = [](auto& ctx, auto in, auto out) {
    using Ctx = std::decay_t<decltype(ctx)>;
    using ByteW = BitVec_T<Ctx, 8>;
    ByteW b = ByteW::from_wires(ctx, in);
    aes_sbox(ctx, b).pack_wires(out);
  };
  // full block driver: in = key[128] ++ pt[128]; out = ct[128].
  auto drv_block = [](auto& ctx, auto in, auto out) {
    using Ctx = std::decay_t<decltype(ctx)>;
    using V128 = BitVec_T<Ctx, 128>;
    V128 key = V128::from_wires(ctx, in);
    V128 pt  = V128::from_wires(ctx, in + 128);
    aes128_encrypt(ctx, pt, key).pack_wires(out);
  };

  // record/replay equivalence.
  std::vector<uint8_t> sin = bits_from_hex("53");   // arbitrary byte
  check("aes_sbox record==live", record_replay(drv_sbox, sin, 8) == clear_run(drv_sbox, sin, 8));

  std::vector<uint8_t> bin = bits_from_hex("000102030405060708090a0b0c0d0e0f");
  std::vector<uint8_t> bpt = bits_from_hex("00112233445566778899aabbccddeeff");
  bin.insert(bin.end(), bpt.begin(), bpt.end());
  check("aes128_encrypt record==live", record_replay(drv_block, bin, 128) == clear_run(drv_block, bin, 128));

  // AND-gate budgets: 28 ANDs / SBox, 5600 ANDs / full block.
  uint64_t sbox_ands  = count_ands(drv_sbox, 8, 8);
  uint64_t block_ands = count_ands(drv_block, 256, 128);
  if (sbox_ands != 28)    printf("  [FAIL] aes_sbox ANDs got %llu want 28\n", (unsigned long long)sbox_ands);
  if (block_ands != 5600) printf("  [FAIL] aes128_encrypt ANDs got %llu want 5600\n", (unsigned long long)block_ands);
  check("aes_sbox == 28 ANDs", sbox_ands == 28);
  check("aes128_encrypt == 5600 ANDs", block_ands == 5600);
}

int main() {
  example();
  test_sbox();
  test_fips197();
  test_openssl_block();
  test_ctr();
  test_ir_plumbing();

  printf("test_crypto_aes: %s\n", g_fail ? "FAILED" : "PASS");
  return g_fail ? 1 : 0;
}
