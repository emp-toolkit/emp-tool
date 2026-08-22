// UInt_T<Ctx,N>: a worked example first, then deterministic correctness
// sweeps cross-checked against host uint32_t (matching the kernels' wrap/trunc
// semantics). Inputs are fed and results revealed through a ClearSession — the
// I/O boundary; the values themselves are pure context-bound circuit values.
// Read example() to see how a normal user writes UInt_T code.
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/ir/context/count.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <random>

using namespace emp;
using Ctx = ClearSession::ctx_t;

using UInt32  = UInt_T<Ctx, 32>;
using DynUInt = DynamicUInt_T<Ctx>;

template <int R>
concept HasU8Popcount = requires(const UInt_T<Ctx, 8>& value) {
  value.template popcount<R>();
};
static_assert(HasU8Popcount<4> && !HasU8Popcount<3>);

// ---- check helpers -------------------------------------------------------

static int g_fail = 0;
static void check(const char* name, bool ok) {
  if (!ok) { printf("  [FAIL] %s\n", name); ++g_fail; }
}
static void check_u(const char* name, uint64_t got, uint64_t want) {
  if (got != want) {
    printf("  [FAIL] %s  got=%llu want=%llu\n", name,
           (unsigned long long)got, (unsigned long long)want);
    ++g_fail;
  }
}
// Low-level: read a runtime-width clear value straight off the wires. Runtime
// This low-level helper keeps the arithmetic sweeps independent of session I/O.
static uint64_t rd_dyn(const DynUInt& u) {
  uint64_t v = 0; for (int i = 0; i < u.width(); ++i) v |= (uint64_t)(u.data()[i] & 1) << i; return v;
}

// Host references with the kernels' wrap semantics (mod 2^32).
static uint32_t shl_w(uint32_t a, unsigned s) { return s >= 32 ? 0u : a << s; }
static uint32_t shr_w(uint32_t a, unsigned s) { return s >= 32 ? 0u : a >> s; }
static uint32_t rotl_w(uint32_t a, unsigned s) { s &= 31; return s ? (a << s) | (a >> (32 - s)) : a; }
static uint32_t rotr_w(uint32_t a, unsigned s) { s &= 31; return s ? (a >> s) | (a << (32 - s)) : a; }

// ---- example: how a user actually writes UInt_T code ---------------------

static void example() {
  ClearSession sess;
  auto a = sess.input<UInt32>(ALICE, 7);
  auto b = sess.input<UInt32>(BOB, 3);

  check("ex sum",      sess.reveal(a + b, PUBLIC).value() == 10);
  check("ex diff",     sess.reveal(a - b, PUBLIC).value() == 4);
  check("ex product",  sess.reveal(a * b, PUBLIC).value() == 21);
  check("ex quotient", sess.reveal(a / b, PUBLIC).value() == 2);
  check("ex mod",      sess.reveal(a % b, PUBLIC).value() == 1);
  check("ex ge",       sess.reveal(a >= b, PUBLIC).value() == true);
  check("ex select",   sess.reveal(a.select((a < b), b), PUBLIC).value() == 7);  // a>=b so keep a

  // Overflow wraps mod 2^32 like a hardware adder.
  auto big = sess.input<UInt32>(ALICE, UINT32_MAX);
  auto one = sess.input<UInt32>(BOB, 1);
  check("ex wrap",     sess.reveal<uint32_t>(big + one, PUBLIC).value() == 0u);
}

// ---- arithmetic / bitwise random sweep (width 32) ------------------------

static void sweep_arith() {
  ClearSession sess;
  std::mt19937_64 rng(0xA1CE12345678ULL);
  for (int i = 0; i < 4000; ++i) {
    uint32_t ia = (uint32_t)rng(), ib = (uint32_t)rng();
    auto a = sess.input<UInt32>(ALICE, ia), b = sess.input<UInt32>(BOB, ib);
    check_u("add", sess.reveal<uint32_t>(a + b, PUBLIC).value(), (uint32_t)(ia + ib));
    check_u("sub", sess.reveal<uint32_t>(a - b, PUBLIC).value(), (uint32_t)(ia - ib));
    check_u("mul", sess.reveal<uint32_t>(a * b, PUBLIC).value(), (uint32_t)(ia * ib));
    check_u("and", sess.reveal<uint32_t>(a & b, PUBLIC).value(), ia & ib);
    check_u("or",  sess.reveal<uint32_t>(a | b, PUBLIC).value(), ia | ib);
    check_u("xor", sess.reveal<uint32_t>(a ^ b, PUBLIC).value(), ia ^ ib);
    check_u("not", sess.reveal<uint32_t>(~a, PUBLIC).value(), ~ia);

    // Division/mod by zero saturates in the kernel; only compare on nonzero.
    if (ib != 0) {
      check_u("div", sess.reveal<uint32_t>(a / b, PUBLIC).value(), ia / ib);
      check_u("mod", sess.reveal<uint32_t>(a % b, PUBLIC).value(), ia % ib);
    }
  }
}

// ---- div/mod by zero: the documented saturating boundary ------------------

static void sweep_div_zero() {
  ClearSession sess;
  // div_full saturates the quotient to all-ones on divide-by-zero.
  auto a = sess.input<UInt32>(ALICE, 12345u), z = sess.input<UInt32>(BOB, 0u);
  check_u("div_by0 -> UINT32_MAX", sess.reveal<uint32_t>(a / z, PUBLIC).value(), UINT32_MAX);
  // Remainder of x/0 is x (no subtraction succeeds).
  check_u("mod_by0 -> dividend",   sess.reveal<uint32_t>(a % z, PUBLIC).value(), 12345u);
}

// ---- comparisons random sweep --------------------------------------------

static void sweep_compare() {
  ClearSession sess;
  std::mt19937_64 rng(0xC0FFEE99ULL);
  for (int i = 0; i < 4000; ++i) {
    uint32_t ia = (uint32_t)rng(), ib = (uint32_t)rng();
    // Occasionally force equality to exercise the == / <= / >= edge.
    if ((i & 7) == 0) ib = ia;
    auto a = sess.input<UInt32>(ALICE, ia), b = sess.input<UInt32>(BOB, ib);
    check("cmp <",  sess.reveal(a < b, PUBLIC).value()  == (ia < ib));
    check("cmp <=", sess.reveal(a <= b, PUBLIC).value() == (ia <= ib));
    check("cmp >",  sess.reveal(a > b, PUBLIC).value()  == (ia > ib));
    check("cmp >=", sess.reveal(a >= b, PUBLIC).value() == (ia >= ib));
    check("cmp ==", sess.reveal(a == b, PUBLIC).value() == (ia == ib));
    check("cmp !=", sess.reveal(a != b, PUBLIC).value() == (ia != ib));
  }
}

// ---- public shifts & rotates, incl. amount >= width ----------------------

static void sweep_shift_public() {
  ClearSession sess;
  std::mt19937_64 rng(0x5417EDULL);
  const uint32_t vs[] = {0u, 1u, 0x80000000u, UINT32_MAX, 0x55555555u, 0xAAAAAAAAu};
  for (uint32_t v : vs) {
    for (int s = 0; s <= 40; ++s) {        // s > 32 must zero (<<, >>)
      auto a = sess.input<UInt32>(ALICE, v);
      check_u("shl pub", sess.reveal<uint32_t>(a << s, PUBLIC).value(), shl_w(v, (unsigned)s));
      check_u("shr pub", sess.reveal<uint32_t>(a >> s, PUBLIC).value(), shr_w(v, (unsigned)s));
    }
    {   // extreme public amount: saturates; i + s must not overflow int
      auto a = sess.input<UInt32>(ALICE, v);
      check_u("shl INT_MAX", sess.reveal<uint32_t>(a << INT_MAX, PUBLIC).value(), 0u);
      auto b = sess.input<UInt32>(ALICE, v);
      check_u("shr INT_MAX", sess.reveal<uint32_t>(b >> INT_MAX, PUBLIC).value(), 0u);
    }
    for (int s = 0; s <= 40; ++s) {        // rotates are mod-32
      auto a = sess.input<UInt32>(ALICE, v);
      check_u("rotl", sess.reveal<uint32_t>(a.rotl(s), PUBLIC).value(), rotl_w(v, (unsigned)s));
      check_u("rotr", sess.reveal<uint32_t>(a.rotr(s), PUBLIC).value(), rotr_w(v, (unsigned)s));
    }
  }
  // A few random rotate amounts for good measure.
  for (int i = 0; i < 500; ++i) {
    uint32_t v = (uint32_t)rng();
    int s = (int)(rng() % 64);
    auto a = sess.input<UInt32>(ALICE, v);
    check_u("rotl rnd", sess.reveal<uint32_t>(a.rotl(s), PUBLIC).value(), rotl_w(v, (unsigned)s));
    check_u("rotr rnd", sess.reveal<uint32_t>(a.rotr(s), PUBLIC).value(), rotr_w(v, (unsigned)s));
  }
}

// ---- secret-amount (barrel) shifts, incl. overflow ----------------------

static void sweep_shift_secret() {
  ClearSession sess;
  const uint32_t vs[] = {0u, 1u, 0x80000000u, UINT32_MAX, 0x55555555u, 0xAAAAAAAAu};
  for (uint32_t v : vs) {
    // 0..64 — values >= 32 (and the high shamt bits beyond log2(32)) must zero.
    for (int s = 0; s <= 64; ++s) {
      auto a  = sess.input<UInt32>(ALICE, v);
      auto sh = sess.input<UInt32>(BOB, (uint32_t)s);
      check_u("shl sec", sess.reveal<uint32_t>(a << sh, PUBLIC).value(), shl_w(v, (unsigned)s));
      check_u("shr sec", sess.reveal<uint32_t>(a >> sh, PUBLIC).value(), shr_w(v, (unsigned)s));
    }
  }
}

// ---- boundary cases ------------------------------------------------------

static void sweep_boundaries() {
  ClearSession sess;
  struct { uint32_t a, b; } cases[] = {
    {0u, 0u}, {0u, 1u}, {1u, 0u},
    {UINT32_MAX, 1u}, {1u, UINT32_MAX},
    {UINT32_MAX, UINT32_MAX}, {0x80000000u, 0x80000000u},
  };
  for (auto c : cases) {
    auto A = sess.input<UInt32>(ALICE, c.a), B = sess.input<UInt32>(BOB, c.b);
    check_u("bnd add", sess.reveal<uint32_t>(A + B, PUBLIC).value(), (uint32_t)(c.a + c.b));
    check_u("bnd sub", sess.reveal<uint32_t>(A - B, PUBLIC).value(), (uint32_t)(c.a - c.b));
    check_u("bnd mul", sess.reveal<uint32_t>(A * B, PUBLIC).value(), (uint32_t)(c.a * c.b));
    if (c.b != 0) {
      check_u("bnd div", sess.reveal<uint32_t>(A / B, PUBLIC).value(), c.a / c.b);
      check_u("bnd mod", sess.reveal<uint32_t>(A % B, PUBLIC).value(), c.a % c.b);
    }
  }
}

// ---- width-changing views: zext/trunc/slice/extract/concat ---------------

static void sweep_views() {
  ClearSession sess;
  std::mt19937_64 rng(0x5EED5EEDULL);
  for (int i = 0; i < 1000; ++i) {
    uint32_t v = (uint32_t)rng();
    auto a = sess.input<UInt32>(ALICE, v);

    check_u("zext<48>",  sess.reveal(a.zext<48>(), PUBLIC).value(), (uint64_t)v);
    check_u("trunc<16>", sess.reveal(a.trunc<16>(), PUBLIC).value(), v & 0xffffu);
    // slice<8,24> picks bits [8,24) -> a 16-bit value.
    check_u("slice<8,24>", sess.reveal(a.slice<8, 24>(), PUBLIC).value(), (v >> 8) & 0xffffu);
    // extract<Base,Width> is slice<Base,Base+Width>.
    check_u("extract<4,12>", sess.reveal(a.extract<4, 12>(), PUBLIC).value(), (v >> 4) & 0xfffu);

    // concat: lo bits are *this, hi bits are the argument (low-at-index-0).
    UInt_T<Ctx, 16> lo = a.trunc<16>();
    auto hi = sess.input<UInt_T<Ctx, 16>>(BOB, (uint16_t)(v >> 16));
    check_u("concat", sess.reveal(lo.concat(hi), PUBLIC).value(), (uint64_t)v);
  }
}

// ---- hamming_weight / leading_zeros / as_signed --------------------------

static void sweep_bitcount_signed() {
  ClearSession sess;
  const uint32_t vs[] = {0u, 1u, UINT32_MAX, 0x80000000u, 0x0F0F0F0Fu, 0xCAFEBABEu, 7u, 0x7FFFFFFFu};
  for (uint32_t v : vs) {
    auto a = sess.input<UInt32>(ALICE, v);
    check_u("hamming_weight", sess.reveal(a.hamming_weight(), PUBLIC).value(),
            (uint64_t)__builtin_popcount(v));
    check_u("leading_zeros", sess.reveal(a.leading_zeros(), PUBLIC).value(),
            v == 0 ? 32u : (uint64_t)__builtin_clz(v));
    // as_signed reinterprets the same wires as two's complement.
    int64_t got = sess.reveal(a.as_signed(), PUBLIC).value();
    check("as_signed", got == (int64_t)(int32_t)v);
  }
}

// ---- runtime-width DynamicUInt_T -------------------------------------

static void sweep_dynamic() {
  ClearSession sess;
  Ctx& ctx = sess.ctx();

  // Construct at a chosen runtime width, read back.
  {
    DynUInt a = DynUInt::constant(ctx, 24, 0xABCDEFu);   // 24-bit value
    check("dyn width", a.width() == 24);
    check_u("dyn read", rd_dyn(a), 0xABCDEFu);
  }

  // resize up (zero-extend) and down (truncate).
  {
    DynUInt a = DynUInt::constant(ctx, 16, 0xBEEFu);
    check_u("dyn resize up",   rd_dyn(a.resize(32)), 0xBEEFull);
    check("dyn resize up width", a.resize(32).width() == 32);
    DynUInt b = DynUInt::constant(ctx, 32, 0xDEADBEEFu);
    check_u("dyn resize down", rd_dyn(b.resize(16)), 0xBEEFull);
    check("dyn resize down width", b.resize(16).width() == 16);
  }

  // to_fixed<M> reaches the session I/O boundary; to_dynamic goes back.
  {
    DynUInt a = DynUInt::constant(ctx, 32, 0x12345678u);
    UInt_T<Ctx, 32> fixed = a.to_fixed<32>();
    check_u("dyn->fixed", sess.reveal(fixed, PUBLIC).value(), 0x12345678u);
    DynUInt back = fixed.to_dynamic();
    check("dyn round-trip width", back.width() == 32);
    check_u("fixed->dyn", rd_dyn(back), 0x12345678u);
  }

  // Arithmetic on the runtime-width form, cross-checked at width 32.
  std::mt19937_64 r2(0xBADC0DEULL);
  for (int i = 0; i < 2000; ++i) {
    uint32_t ia = (uint32_t)r2(), ib = (uint32_t)r2();
    DynUInt a = DynUInt::constant(ctx, 32, ia), b = DynUInt::constant(ctx, 32, ib);
    check_u("dyn add", rd_dyn(a + b), (uint32_t)(ia + ib));
    check_u("dyn sub", rd_dyn(a - b), (uint32_t)(ia - ib));
    check_u("dyn mul", rd_dyn(a * b), (uint32_t)(ia * ib));
    check_u("dyn and", rd_dyn(a & b), ia & ib);
    check_u("dyn or",  rd_dyn(a | b), ia | ib);
    check_u("dyn xor", rd_dyn(a ^ b), ia ^ ib);
    check("dyn lt", sess.reveal(a < b, PUBLIC).value() == (ia < ib));   // comparison yields a Bit_T
    if (ib != 0) {
      check_u("dyn div", rd_dyn(a / b), ia / ib);
      check_u("dyn mod", rd_dyn(a % b), ia % ib);
    }
  }

  // hamming_weight on the runtime form.
  {
    DynUInt a = DynUInt::constant(ctx, 32, 0xCAFEBABEu);
    check_u("dyn hamming", rd_dyn(a.hamming_weight()),
            (uint64_t)__builtin_popcount(0xCAFEBABEu));
  }
}

// ---- structured-kernel gate costs ----------------------------------------

static void sweep_kernel_costs() {
  constexpr int N = 8;
  std::array<CountCtx::Wire, N> a{}, b{}, out{};

  {
    CountCtx ctx;
    kernel::equal(ctx, a.data(), b.data(), N);
    check("equal N-1 ANDs", ctx.ands == N - 1);
  }
  {
    CountCtx ctx;
    kernel::less_than(ctx, a.data(), b.data(), N);
    check("less_than N ANDs", ctx.ands == N);
    check("less_than no difference wires", ctx.xors == 3 * N);
  }
  {
    CountCtx ctx;
    kernel::mul_full(ctx, out.data(), a.data(), b.data(), N);
    check("multiply seeds first row", ctx.ands == N * N - N + 1);
    check("multiply omits zero-row XORs", ctx.xors == 2 * (N - 1) * (N - 1));
  }
  CountCtx rem_ctx;
  kernel::div_full(rem_ctx, nullptr, out.data(), a.data(), b.data(), N);
  check("remainder skips quotient NOTs", rem_ctx.nots == 0);

  CountCtx quot_ctx;
  kernel::div_full(quot_ctx, out.data(), nullptr, a.data(), b.data(), N);
  check("quotient still emits result bits", quot_ctx.nots == N);
  check("division and remainder share non-NOT gates",
        quot_ctx.ands == rem_ctx.ands && quot_ctx.xors == rem_ctx.xors &&
        quot_ctx.consts == rem_ctx.consts);

  {
    CountCtx ctx;
    auto value = UInt_T<CountCtx, N>::constant(ctx, 0xA5);
    (void)value.hamming_weight();
    check("fixed hamming seeds first bit", ctx.ands == (N - 1) * (kernel::bits_for(N) - 1));
  }
  {
    CountCtx ctx;
    auto value = DynamicUInt_T<CountCtx>::constant(ctx, N, 0xA5);
    (void)value.hamming_weight();
    check("dynamic hamming seeds first bit", ctx.ands == (N - 1) * (kernel::bits_for(N) - 1));
  }
}

static void sweep_kernel_exhaustive() {
  ClearSession sess;
  using U4 = UInt_T<Ctx, 4>;
  for (uint64_t x = 0; x < 16; ++x) {
    for (uint64_t y = 0; y < 16; ++y) {
      auto a = sess.input<U4>(ALICE, x);
      auto b = sess.input<U4>(BOB, y);
      check("kernel exhaustive equal", sess.reveal(a == b, PUBLIC).value() == (x == y));
      check("kernel exhaustive less_than", sess.reveal(a < b, PUBLIC).value() == (x < y));
      check_u("kernel exhaustive multiply", sess.reveal(a * b, PUBLIC).value(), (x * y) & 15);
      if (y != 0)
        check_u("kernel exhaustive remainder", sess.reveal(a % b, PUBLIC).value(), x % y);
    }
  }
}

int main() {
  example();
  sweep_arith();
  sweep_div_zero();
  sweep_compare();
  sweep_shift_public();
  sweep_shift_secret();
  sweep_boundaries();
  sweep_views();
  sweep_bitcount_signed();
  sweep_dynamic();
  sweep_kernel_costs();
  sweep_kernel_exhaustive();

  printf("test_uint: %s\n", g_fail ? "FAILED" : "PASS");
  return g_fail ? 1 : 0;
}
