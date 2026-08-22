// Int_T<Ctx,N> — a two's-complement signed integer. Read example() first;
// the rest verifies each operator against host int32_t / int64_t (i.e. the real
// two's-complement hardware behavior). All arithmetic wraps mod 2^N, right shift
// is ARITHMETIC (sign-filling), and division/remainder truncate toward zero with
// the remainder taking the dividend's sign. Inputs are fed and results revealed
// through a ClearSession — the I/O boundary; the values are pure circuit values.
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include <climits>
#include <cstdint>
#include <cstdio>
#include <random>
using namespace emp;
using Ctx = ClearSession::ctx_t;

using Int32   = Int_T<Ctx, 32>;
using UInt32  = UInt_T<Ctx, 32>;
using IntDyn  = DynamicInt_T<Ctx>;

// ---- local check helpers (no global backend, no raw .w in the examples) ----

static int g_fail = 0;
static void check(const char* name, bool ok) {
  if (!ok) { printf("  [FAIL] %s\n", name); ++g_fail; }
}
static void check_eq(const char* name, int64_t got, int64_t want) {
  if (got != want) {
    printf("  [FAIL] %s: got %lld want %lld\n", name, (long long)got, (long long)want);
    ++g_fail;
  }
}
// Low-level: read a runtime-width signed clear value straight off the wires
// (sign-extending the top bit).
static int64_t rd_dyn(const IntDyn& x) {
  const int n = x.width();
  uint64_t v = 0; for (int i = 0; i < n; ++i) v |= (uint64_t)(x.data()[i] & 1) << i;
  if (n < 64 && ((v >> (n - 1)) & 1)) v |= ~((uint64_t(1) << n) - 1);
  return (int64_t)v;
}

// ---- example: how a normal user creates inputs, computes, and reveals -------

static void example() {
  ClearSession sess;
  auto a = sess.input<Int32>(ALICE, -7);   // ALICE feeds -7
  auto b = sess.input<Int32>(BOB,    3);   // BOB feeds 3

  check("example sum",  sess.reveal(a + b, PUBLIC).value() == -4);                 // -7 + 3
  check("example diff", sess.reveal(a - b, PUBLIC).value() == -10);                // -7 - 3
  check("example prod", sess.reveal(a * b, PUBLIC).value() == -21);                // -7 * 3
  check("example quot", sess.reveal(a / b, PUBLIC).value() == -2);                 // truncate toward 0
  check("example rem",  sess.reveal(a % b, PUBLIC).value() == -1);                 // remainder takes dividend sign
  check("example neg",  sess.reveal(-a, PUBLIC).value() == 7);
  check("example lt",   sess.reveal(a < b, PUBLIC).value() == true);               // signed: -7 < 3
  check("example ge0",  sess.reveal(a >= sess.input<Int32>(PUBLIC, 0), PUBLIC).value() == false);
  check("example asr",  sess.reveal(a >> 1, PUBLIC).value() == -4);                // arithmetic shift right
  check("example cast", sess.reveal<int>(a + b, PUBLIC).value() == -4);            // reveal<T> casts for readability
}

// ---- reference: deterministic wrapping signed arithmetic (host) ------------

static int32_t add_w(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t sub_w(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static int32_t mul_w(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static int32_t neg_w(int32_t a)            { return (int32_t)(0u - (uint32_t)a); }
static int32_t shl_w(int32_t a, unsigned s) { return s >= 32 ? 0 : (int32_t)((uint32_t)a << s); }
static int32_t asr_w(int32_t a, unsigned s) {                 // arithmetic shift right
  if (s >= 32) return a < 0 ? -1 : 0;
  return (int32_t)(a >> (int)s);                              // hw-typical ASR
}

// ---- deterministic random sweeps -------------------------------------------

static std::mt19937_64 rng(0x51A3D7C9ULL);   // FIXED seed: reproducible, no time

static void sweep_arith() {
  ClearSession sess;
  for (int i = 0; i < 4000; ++i) {
    int32_t ia = (int32_t)(uint32_t)rng();
    int32_t ib = (int32_t)(uint32_t)rng();
    auto a = sess.input<Int32>(ALICE, ia), b = sess.input<Int32>(BOB, ib);
    check_eq("add", sess.reveal(a + b, PUBLIC).value(), add_w(ia, ib));
    check_eq("sub", sess.reveal(a - b, PUBLIC).value(), sub_w(ia, ib));
    check_eq("mul", sess.reveal(a * b, PUBLIC).value(), mul_w(ia, ib));
    check_eq("and", sess.reveal(a & b, PUBLIC).value(), (int32_t)(ia & ib));
    check_eq("or",  sess.reveal(a | b, PUBLIC).value(), (int32_t)(ia | ib));
    check_eq("xor", sess.reveal(a ^ b, PUBLIC).value(), (int32_t)(ia ^ ib));
    check_eq("not", sess.reveal(~a, PUBLIC).value(),    (int32_t)(~ia));
    check_eq("neg", sess.reveal(-a, PUBLIC).value(),    neg_w(ia));
  }
}

static void sweep_div_mod() {
  ClearSession sess;
  for (int i = 0; i < 4000; ++i) {
    int32_t ia = (int32_t)(uint32_t)rng();
    int32_t ib = (int32_t)(uint32_t)rng();
    if (ib == 0) continue;                       // division by zero is undefined here
    if (ia == INT32_MIN && ib == -1) continue;   // INT_MIN/-1 is a UB precondition
    auto a = sess.input<Int32>(ALICE, ia), b = sess.input<Int32>(BOB, ib);
    check_eq("div (truncate toward 0)", sess.reveal(a / b, PUBLIC).value(), (int32_t)(ia / ib));
    check_eq("mod (sign of dividend)",  sess.reveal(a % b, PUBLIC).value(), (int32_t)(ia % ib));
  }
}

static void sweep_compare() {
  ClearSession sess;
  for (int i = 0; i < 4000; ++i) {
    int32_t ia = (int32_t)(uint32_t)rng();
    int32_t ib = (int32_t)(uint32_t)rng();
    auto a = sess.input<Int32>(ALICE, ia), b = sess.input<Int32>(BOB, ib);
    check("signed <",  sess.reveal(a <  b, PUBLIC).value() == (ia <  ib));
    check("signed <=", sess.reveal(a <= b, PUBLIC).value() == (ia <= ib));
    check("signed >",  sess.reveal(a >  b, PUBLIC).value() == (ia >  ib));
    check("signed >=", sess.reveal(a >= b, PUBLIC).value() == (ia >= ib));
    check("signed ==", sess.reveal(a == b, PUBLIC).value() == (ia == ib));
    check("signed !=", sess.reveal(a != b, PUBLIC).value() == (ia != ib));
  }
}

// ---- shifts: logical-left, arithmetic-right, incl. shamt >= width ----------

static const int32_t kShiftVals[] = {
    0, 1, -1, 7, -7, INT32_MAX, INT32_MIN, INT32_MIN + 1,
    0x55555555, (int32_t)0xAAAAAAAA};

static void sweep_shift_public() {
  ClearSession sess;
  for (int32_t v : kShiftVals)
    for (unsigned s = 0; s <= 33; ++s) {         // include shamt > width
      auto a = sess.input<Int32>(ALICE, v);
      check_eq("public <<", sess.reveal(a << (int)s, PUBLIC).value(), shl_w(v, s));
      check_eq("public >> (arith)", sess.reveal(a >> (int)s, PUBLIC).value(), asr_w(v, s));
    }
  for (int32_t v : kShiftVals) {
    // Extreme public amount: << zeros, >> sign-fills — and the i + s
    // index arithmetic must not overflow int.
    auto a = sess.input<Int32>(ALICE, v);
    check_eq("public << INT_MAX", sess.reveal(a << INT_MAX, PUBLIC).value(),
             (int32_t)0);
    auto b = sess.input<Int32>(ALICE, v);
    check_eq("public >> INT_MAX (arith)",
             sess.reveal(b >> INT_MAX, PUBLIC).value(),
             v < 0 ? (int32_t)-1 : (int32_t)0);
  }
}

// Secret (barrel) shift amount. shamt >= 32 exercises the overflow path:
// `<<` must zero, `>>` must sign-fill (all bits = msb).
static void sweep_shift_secret() {
  ClearSession sess;
  for (int32_t v : kShiftVals)
    for (unsigned s = 0; s <= 65; ++s) {
      auto a = sess.input<Int32>(ALICE, v);
      auto sh = sess.input<UInt32>(BOB, (uint64_t)s);
      check_eq("secret <<", sess.reveal(a << sh, PUBLIC).value(), shl_w(v, s));
      check_eq("secret >> (arith)", sess.reveal(a >> sh, PUBLIC).value(), asr_w(v, s));
    }
}

// ---- boundary cases: 0, -1, 1, most-negative, most-positive ----------------

static void boundary_cases() {
  ClearSession sess;
  struct { int32_t a, b; } cases[] = {
      {INT32_MAX, 1}, {INT32_MIN, -1}, {INT32_MAX, INT32_MAX},
      {INT32_MIN, INT32_MIN}, {0, 0}, {-1, -1}, {1, -1}, {INT32_MIN, 1},
  };
  for (auto c : cases) {
    auto A = sess.input<Int32>(ALICE, c.a), B = sess.input<Int32>(BOB, c.b);
    check_eq("boundary +", sess.reveal(A + B, PUBLIC).value(), add_w(c.a, c.b));
    check_eq("boundary -", sess.reveal(A - B, PUBLIC).value(), sub_w(c.a, c.b));
    check_eq("boundary *", sess.reveal(A * B, PUBLIC).value(), mul_w(c.a, c.b));
  }
  // negate at the boundaries (-INT_MIN wraps back to INT_MIN).
  for (int32_t v : {0, 1, -1, 7, -7, INT32_MAX, INT32_MIN, INT32_MIN + 1}) {
    auto a = sess.input<Int32>(ALICE, v);
    check_eq("boundary neg", sess.reveal(-a, PUBLIC).value(), neg_w(v));
  }
}

// ---- width changes: sext / trunc / as_unsigned -----------------------------

static void width_changes() {
  ClearSession sess;
  for (int32_t v : {0, 1, -1, 7, -7, INT32_MAX, INT32_MIN, (int32_t)0xDEADBEEF}) {
    auto a = sess.input<Int32>(ALICE, v);
    // sext widens, replicating the sign bit.
    check_eq("sext<48>", sess.reveal(a.sext<48>(), PUBLIC).value(), (int64_t)v);
    check_eq("sext<56>", sess.reveal(a.sext<56>(), PUBLIC).value(), (int64_t)v);
    // trunc keeps the low bits (a signed 16-bit view of them).
    check_eq("trunc<16>", sess.reveal(a.trunc<16>(), PUBLIC).value(), (int16_t)(v & 0xffff));
    // as_unsigned bit-casts the same wires (zero gates).
    check_eq("as_unsigned", (int64_t)sess.reveal(a.as_unsigned(), PUBLIC).value(), (int64_t)(uint32_t)v);
    // round-trip: signed -> unsigned -> signed is the identity.
    check_eq("as_unsigned->as_signed", sess.reveal(a.as_unsigned().as_signed(), PUBLIC).value(), (int64_t)v);
  }
}

// ---- runtime-width DynamicInt_T --------------------------------------

static void runtime_width_section() {
  ClearSession sess;
  Ctx& ctx = sess.ctx();
  const int W = 24;

  // ::constant feeds a value sign-extended into a runtime-width wire vector.
  IntDyn a = IntDyn::constant(ctx, W, -1000);
  IntDyn b = IntDyn::constant(ctx, W,   333);
  check("runtime width()", a.width() == W);
  check_eq("runtime add", rd_dyn(a + b), -667);
  check_eq("runtime sub", rd_dyn(a - b), -1333);
  check_eq("runtime mul", rd_dyn(a * b),
           (int64_t)((int32_t)(((uint32_t)(-1000) * 333u) << 8) >> 8));   // wrap to 24 bits, sign-extend
  check_eq("runtime div", rd_dyn(a / b), -1000 / 333);   // -3, truncate toward 0
  check_eq("runtime mod", rd_dyn(a % b), -1000 % 333);   // -1, sign of dividend
  check("runtime lt", sess.reveal(a < b, PUBLIC).value() == true);   // signed: -1000 < 333; comparison yields a Bit_T

  // arithmetic right shift on a negative runtime value sign-fills.
  check_eq("runtime asr", rd_dyn(a >> 2), -1000 >> 2);   // -250
  check_eq("runtime shl", rd_dyn(b << 3), 333 << 3);

  // resize sign-extends when widening and truncates when narrowing.
  check_eq("runtime resize up",   rd_dyn(a.resize(40)), -1000);
  check_eq("runtime resize down", rd_dyn(a.resize(12)),
           (int64_t)((int16_t)(((uint16_t)(-1000)) << 4) >> 4));   // sign-extend low 12 bits

  // deterministic runtime sweep at width 28 against the host (masked) reference.
  const int w = 28;
  const int64_t mask = (int64_t(1) << w) - 1;
  auto sx = [&](int64_t x) -> int64_t {           // sign-extend a w-bit value
    x &= mask;
    if (x & (int64_t(1) << (w - 1))) x |= ~mask;
    return x;
  };
  for (int i = 0; i < 2000; ++i) {
    int64_t ia = sx((int64_t)rng());
    int64_t ib = sx((int64_t)rng());
    IntDyn x = IntDyn::constant(ctx, w, ia), y = IntDyn::constant(ctx, w, ib);
    check_eq("dyn add", rd_dyn(x + y), sx(ia + ib));
    check_eq("dyn sub", rd_dyn(x - y), sx(ia - ib));
    check_eq("dyn mul", rd_dyn(x * y), sx(ia * ib));
    if (ib != 0) {
      check_eq("dyn div", rd_dyn(x / y), sx(ia / ib));
      check_eq("dyn mod", rd_dyn(x % y), sx(ia % ib));
    }
    check("dyn lt", sess.reveal(x < y, PUBLIC).value() == (ia < ib));   // comparison yields a Bit_T
  }
}

int main() {
  example();
  sweep_arith();
  sweep_div_mod();
  sweep_compare();
  sweep_shift_public();
  sweep_shift_secret();
  boundary_cases();
  width_changes();
  runtime_width_section();
  printf("test_int: %s\n", g_fail ? "FAILED" : "PASS");
  return g_fail ? 1 : 0;
}
