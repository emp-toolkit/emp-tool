// Float_T<Ctx,W> (W = 16, 32, 64): IEEE binary{16,32,64} arithmetic,
// comparison, classification, and sign ops. Binary/unary/ternary arithmetic
// replays the on-disk fp<W>_<op>.empbc builtins through the context, so the run
// recipe points EMP_CIRCUIT_DIR at emp-tool/ir/files. Each result is checked
// against the host scalar (float for fp16/fp32, double for fp64) bit-exactly:
// every arithmetic builtin is correctly rounded except fma and rsqrt, which are
// two correctly rounded steps (round(round(a*b)+c) and recip(sqrt(x))).
// Inputs are fed and results revealed through a ClearSession — the I/O boundary;
// the values themselves are pure context-bound circuit values.
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include "emp-tool/circuits/float.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <type_traits>
using namespace emp;
using Ctx = ClearSession::ctx_t;

using F32 = Float_T<Ctx, 32>;

static int g_fail = 0;
static void check(const char* name, bool ok) {
  if (!ok) { printf("  [FAIL] %s\n", name); ++g_fail; }
}
// got/want reporter. Host floats compare by IEEE bit pattern, so -0.0 and +0.0
// differ; any two NaNs match.
template <class T>
static void check_eq(const char* name, T got, T want) {
  bool same;
  if constexpr (std::is_floating_point_v<T>)
    same = (std::isnan(got) && std::isnan(want)) || std::memcmp(&got, &want, sizeof(T)) == 0;
  else
    same = got == want;
  if (!same) {
    printf("  [FAIL] %s  got %.17g want %.17g\n", name, (double)got, (double)want);
    ++g_fail;
  }
}
// Raw IEEE bit pattern of a clear host value at width W, and of a revealed
// Float_T — used for exact bit-equality of the value against host arithmetic.
template <int W>
static uint64_t host_bits(typename FloatTraits<W>::host_t v) { return FloatTraits<W>::to_bits(v); }

// Round a double (the wide accumulator we compute references in) to the
// width-W representable grid — the codec round-trip is the IEEE round-to-nearest
// for that format. fp16's host scalar is float, so its arithmetic must be done
// in a wider type and then rounded back here; fp32/fp64 round to themselves.
template <int W>
static typename FloatTraits<W>::host_t round_to_w(double v) {
  return FloatTraits<W>::from_bits(FloatTraits<W>::to_bits((typename FloatTraits<W>::host_t)v));
}

// ---------------------------------------------------------------------------
// example(): how a normal user builds float inputs, computes with operators,
// and reveals. ALICE and BOB feed clear values through the session.
// ---------------------------------------------------------------------------
static void example() {
  ClearSession sess;

  auto a = sess.input<F32>(ALICE, 1.5f), b = sess.input<F32>(BOB, 2.25f);
  check("example add", sess.reveal(a + b, PUBLIC).value() == 3.75f);
  check("example sub", sess.reveal(b - a, PUBLIC).value() == 0.75f);
  check("example mul", sess.reveal(a * b, PUBLIC).value() == 3.375f);
  check("example div", sess.reveal(b / a, PUBLIC).value() == 1.5f);
  check("example lt",  sess.reveal(a < b, PUBLIC).value() == true);
  check("example abs", sess.reveal((-a).abs(), PUBLIC).value() == 1.5f);

  // reveal<T>() casts the host float to a named type for readability.
  auto nine = sess.input<F32>(ALICE, 9.0f);
  check("example sqrt", sess.reveal<float>(nine.sqrt(), PUBLIC).value() == 3.0f);
}

// ---------------------------------------------------------------------------
// Worked examples on exactly-representable values across all three widths.
// Every value here is exact in binary16/32/64, so all checks are bit-exact.
// ---------------------------------------------------------------------------
template <int W>
static void width_examples(const char* tag) {
  using F = Float_T<Ctx, W>;
  using host_t = typename FloatTraits<W>::host_t;
  ClearSession sess;
  auto C = [&](host_t v) { return sess.input<F>(ALICE, v); };
  auto name = [&](const char* op) {
    static char buf[64]; std::snprintf(buf, sizeof buf, "%s %s", tag, op); return buf;
  };

  F a = C((host_t)2.0), b = C((host_t)3.0), nine = C((host_t)9.0), z = C((host_t)0.0);

  check_eq<host_t>(name("add"),   sess.reveal(a + b, PUBLIC).value(), (host_t)5.0);
  check_eq<host_t>(name("sub"),   sess.reveal(a - b, PUBLIC).value(), (host_t)-1.0);
  check_eq<host_t>(name("mul"),   sess.reveal(a * b, PUBLIC).value(), (host_t)6.0);
  check_eq<host_t>(name("div"),   sess.reveal(b / a, PUBLIC).value(), (host_t)1.5);
  check_eq<host_t>(name("sqr"),   sess.reveal(a.sqr(), PUBLIC).value(), (host_t)4.0);
  check_eq<host_t>(name("min"),   sess.reveal(a.min(b), PUBLIC).value(), (host_t)2.0);
  check_eq<host_t>(name("max"),   sess.reveal(a.max(b), PUBLIC).value(), (host_t)3.0);
  check_eq<host_t>(name("fma"),   sess.reveal(a.fma(b, a), PUBLIC).value(), (host_t)8.0);  // 2*3 + 2

  // sqrt and recip are correctly rounded; rsqrt is recip(sqrt(x)).
  check_eq<host_t>(name("sqrt"),  sess.reveal(nine.sqrt(), PUBLIC).value(),  (host_t)3.0);
  check_eq<host_t>(name("recip"), sess.reveal(a.recip(), PUBLIC).value(),    (host_t)0.5);
  check_eq<host_t>(name("rsqrt"), sess.reveal(nine.rsqrt(), PUBLIC).value(), round_to_w<W>(1.0 / 3.0));

  // sign ops are pure wiring (set/flip the MSB), so exact at every width.
  check_eq<host_t>(name("neg"),       sess.reveal(-a, PUBLIC).value(), (host_t)-2.0);
  check_eq<host_t>(name("abs"),       sess.reveal((-a).abs(), PUBLIC).value(), (host_t)2.0);
  check_eq<host_t>(name("copysign"),  sess.reveal(a.copysign(-b), PUBLIC).value(), (host_t)-2.0);

  // comparisons / classifiers return Bit_T.
  check(name("lt true"),  sess.reveal(a.less_than(b), PUBLIC).value() == true);
  check(name("lt false"), sess.reveal(b.less_than(a), PUBLIC).value() == false);
  check(name("le"),       sess.reveal(a.less_equal(a), PUBLIC).value() == true);
  check(name("eq"),       sess.reveal(a.equal(a), PUBLIC).value() == true);
  check(name("ne"),       sess.reveal(a.not_equal(b), PUBLIC).value() == true);
  check(name("ge"),       sess.reveal(b.greater_equal(a), PUBLIC).value() == true);
  check(name("gt"),       sess.reveal(a.greater_than(b), PUBLIC).value() == false);
  check(name("iszero"),   sess.reveal(z.is_zero(), PUBLIC).value() == true);
  check(name("iszero f"), sess.reveal(a.is_zero(), PUBLIC).value() == false);
  check(name("isnan"),    sess.reveal(a.is_nan(), PUBLIC).value() == false);
  check(name("isinf"),    sess.reveal(a.is_inf(), PUBLIC).value() == false);

  // select(sel, other): sel ? other : *this.
  auto t = sess.input<Bit_T<Ctx>>(ALICE, true), f = sess.input<Bit_T<Ctx>>(ALICE, false);
  check_eq<host_t>(name("select t"), sess.reveal(a.select(t, b), PUBLIC).value(), (host_t)3.0);
  check_eq<host_t>(name("select f"), sess.reveal(a.select(f, b), PUBLIC).value(), (host_t)2.0);
}

// ---------------------------------------------------------------------------
// Deterministic random sweeps vs host arithmetic, BIT-EXACT: the reference is
// computed in a wide accumulator (double) and rounded back to width W per
// operation (round_to_w), matching the format's grid exactly. A double carries
// at least 2p + 2 bits for p = 11 and 24, so for + - * / and sqrt that extra
// rounding cannot change the result. fma is NOT a single fused rounding — it is
// round(round(a*b)+c), so the product is sequenced through a named variable;
// rsqrt likewise is recip(sqrt(x)). Seed is FIXED — no time/Date.
// ---------------------------------------------------------------------------
template <int W>
static void random_sweep(const char* tag, int runs) {
  using F = Float_T<Ctx, W>;
  using host_t = typename FloatTraits<W>::host_t;
  ClearSession sess;
  std::mt19937_64 rng(0xF10A7ULL + (uint64_t)W);
  // Spread over several magnitude decades, both signs, avoiding inf/nan.
  std::uniform_real_distribution<double> mant(-1.0, 1.0);
  std::uniform_int_distribution<int> ex(-12, 12);

  auto bits = [](host_t v) { return host_bits<W>(v); };
  int bad_exact = 0;

  for (int i = 0; i < runs; ++i) {
    // Operands snapped to the width's grid so the host reference and circuit
    // see the identical inputs.
    double xa = mant(rng) * std::pow(2.0, ex(rng));
    double xb = mant(rng) * std::pow(2.0, ex(rng));
    host_t da = round_to_w<W>(xa);
    host_t db = round_to_w<W>(xb);
    if (db == (host_t)0.0) db = (host_t)1.0;  // keep div well-defined
    F a = sess.input<F>(ALICE, da), b = sess.input<F>(BOB, db);

    // Bit-exact: reference computed wide, rounded once to width W.
    if (bits(sess.reveal(a + b, PUBLIC).value()) != bits(round_to_w<W>((double)da + (double)db))) ++bad_exact;
    if (bits(sess.reveal(a - b, PUBLIC).value()) != bits(round_to_w<W>((double)da - (double)db))) ++bad_exact;
    if (bits(sess.reveal(a * b, PUBLIC).value()) != bits(round_to_w<W>((double)da * (double)db))) ++bad_exact;
    if (bits(sess.reveal(a / b, PUBLIC).value()) != bits(round_to_w<W>((double)da / (double)db))) ++bad_exact;
    if (bits(sess.reveal(a.min(b), PUBLIC).value()) != bits((host_t)std::fmin(da, db))) ++bad_exact;
    if (bits(sess.reveal(a.max(b), PUBLIC).value()) != bits((host_t)std::fmax(da, db))) ++bad_exact;

    // fma = round(round(a*b) + c): two roundings, product sequenced first.
    host_t prod = round_to_w<W>((double)da * (double)db);
    host_t fma_want = round_to_w<W>((double)prod + (double)da);
    if (bits(sess.reveal(a.fma(b, a), PUBLIC).value()) != bits(fma_want)) ++bad_exact;

    // sqrt and recip are correctly rounded; rsqrt is recip(sqrt(x)), rounded
    // twice. Zero and overflowing results (1/0, 1/tiny) are compared too.
    host_t ha = round_to_w<W>(std::fabs((double)da));
    F pa = sess.input<F>(ALICE, ha);
    host_t sqrt_want = round_to_w<W>(std::sqrt((double)ha));
    if (bits(sess.reveal(pa.sqrt(), PUBLIC).value()) != bits(sqrt_want)) ++bad_exact;
    if (bits(sess.reveal(a.recip(), PUBLIC).value()) != bits(round_to_w<W>(1.0 / (double)da))) ++bad_exact;
    if (bits(sess.reveal(pa.rsqrt(), PUBLIC).value()) != bits(round_to_w<W>(1.0 / (double)sqrt_want))) ++bad_exact;
  }
  char nm[96];
  std::snprintf(nm, sizeof nm, "%s sweep bit-exact (+,-,*,/,min,max,fma,sqrt,recip,rsqrt)", tag);
  check(nm, bad_exact == 0);
}

// ---------------------------------------------------------------------------
// Comparison sweep: every ordering predicate must agree with the host across
// equal / less / greater / NaN-involving operands.
// ---------------------------------------------------------------------------
template <int W>
static void compare_sweep(const char* tag, int runs) {
  using F = Float_T<Ctx, W>;
  using host_t = typename FloatTraits<W>::host_t;
  ClearSession sess;
  std::mt19937_64 rng(0xC0FFEEULL + (uint64_t)W);
  std::uniform_real_distribution<double> mant(-1.0, 1.0);
  std::uniform_int_distribution<int> ex(-8, 8);
  std::uniform_int_distribution<int> tie(0, 4);
  int bad = 0;

  for (int i = 0; i < runs; ++i) {
    // Snap operands to the width's grid (idempotent through F::constant) so the
    // host comparison sees exactly the value the circuit holds.
    host_t da = round_to_w<W>(mant(rng) * std::pow(2.0, ex(rng)));
    host_t db = (tie(rng) == 0) ? da   // force exact ties some of the time
                                : round_to_w<W>(mant(rng) * std::pow(2.0, ex(rng)));
    F a = sess.input<F>(ALICE, da), b = sess.input<F>(BOB, db);
    if (sess.reveal(a.less_than(b), PUBLIC).value()     != (da <  db)) ++bad;
    if (sess.reveal(a.less_equal(b), PUBLIC).value()    != (da <= db)) ++bad;
    if (sess.reveal(a.greater_than(b), PUBLIC).value()  != (da >  db)) ++bad;
    if (sess.reveal(a.greater_equal(b), PUBLIC).value() != (da >= db)) ++bad;
    if (sess.reveal(a.equal(b), PUBLIC).value()         != (da == db)) ++bad;
    if (sess.reveal(a.not_equal(b), PUBLIC).value()     != (da != db)) ++bad;
  }
  char nm[64]; std::snprintf(nm, sizeof nm, "%s compare sweep", tag);
  check(nm, bad == 0);
}

// ---------------------------------------------------------------------------
// Boundary / special operands: 0, -0, +inf, -inf, NaN, denormal, max.
// Checks the classifiers and the sign ops on the edge of the format.
// ---------------------------------------------------------------------------
template <int W>
static void boundary_cases(const char* tag) {
  using F = Float_T<Ctx, W>;
  using host_t = typename FloatTraits<W>::host_t;
  ClearSession sess;
  auto name = [&](const char* op) {
    static char buf[64]; std::snprintf(buf, sizeof buf, "%s %s", tag, op); return buf;
  };

  const host_t inf = std::numeric_limits<host_t>::infinity();
  const host_t nan = std::numeric_limits<host_t>::quiet_NaN();
  // smallest positive subnormal and largest finite at THIS width, in the host
  // scalar (round-tripped through the width's bit codec so it is representable).
  host_t denorm, maxv;
  if constexpr (W == 16) {
    denorm = emp_half_to_float(0x0001);   // 2^-24
    maxv   = emp_half_to_float(0x7BFF);   // 65504
  } else {
    denorm = std::numeric_limits<host_t>::denorm_min();
    maxv   = std::numeric_limits<host_t>::max();
  }

  F pos_zero = sess.input<F>(ALICE, (host_t)0.0);
  F neg_zero = sess.input<F>(ALICE, (host_t)-0.0);
  F pinf = sess.input<F>(ALICE, inf), ninf = sess.input<F>(ALICE, -inf);
  F qnan = sess.input<F>(ALICE, nan);
  F den = sess.input<F>(ALICE, denorm), big = sess.input<F>(ALICE, maxv);

  // classifiers.
  check(name("iszero +0"), sess.reveal(pos_zero.is_zero(), PUBLIC).value() == true);
  check(name("iszero -0"), sess.reveal(neg_zero.is_zero(), PUBLIC).value() == true);
  check(name("iszero den"), sess.reveal(den.is_zero(), PUBLIC).value() == false);
  check(name("isinf +inf"), sess.reveal(pinf.is_inf(), PUBLIC).value() == true);
  check(name("isinf -inf"), sess.reveal(ninf.is_inf(), PUBLIC).value() == true);
  check(name("isinf big"), sess.reveal(big.is_inf(), PUBLIC).value() == false);
  check(name("isnan nan"), sess.reveal(qnan.is_nan(), PUBLIC).value() == true);
  check(name("isnan +inf"), sess.reveal(pinf.is_nan(), PUBLIC).value() == false);
  check(name("isnan big"), sess.reveal(big.is_nan(), PUBLIC).value() == false);

  // -0 and +0 compare equal in IEEE.
  check(name("+0 == -0"), sess.reveal(pos_zero.equal(neg_zero), PUBLIC).value() == true);

  // sign ops on the edges: bit patterns must match the host exactly.
  check_eq<uint64_t>(name("abs -inf"), host_bits<W>(sess.reveal(ninf.abs(), PUBLIC).value()), host_bits<W>(inf));
  check_eq<uint64_t>(name("neg +inf"), host_bits<W>(sess.reveal(-pinf, PUBLIC).value()), host_bits<W>(-inf));
  check_eq<uint64_t>(name("abs -0"),   host_bits<W>(sess.reveal(neg_zero.abs(), PUBLIC).value()), host_bits<W>((host_t)0.0));
  check_eq<uint64_t>(name("copysign den<-big"),
                     host_bits<W>(sess.reveal(den.copysign(-big), PUBLIC).value()), host_bits<W>(-denorm));

  // arithmetic that overflows the format goes to inf; inf - inf is NaN.
  check(name("max+max -> inf"), sess.reveal((big + big).is_inf(), PUBLIC).value() == true);
  check(name("fma overflow -> inf"),
        sess.reveal(big.fma(sess.input<F>(ALICE, (host_t)2.0), big).is_inf(), PUBLIC).value() == true);
  check(name("inf - inf -> nan"), sess.reveal((pinf - pinf).is_nan(), PUBLIC).value() == true);
  check(name("1/inf -> 0"), sess.reveal(pinf.recip().is_zero(), PUBLIC).value() == true);

  // NaN is unordered: every ordering predicate is false, ne is true.
  F one = sess.input<F>(ALICE, (host_t)1.0);
  check(name("nan<1 false"),  sess.reveal(qnan.less_than(one), PUBLIC).value() == false);
  check(name("nan>1 false"),  sess.reveal(qnan.greater_than(one), PUBLIC).value() == false);
  check(name("nan==1 false"), sess.reveal(qnan.equal(one), PUBLIC).value() == false);
  check(name("nan!=1 true"),  sess.reveal(qnan.not_equal(one), PUBLIC).value() == true);
  check(name("nan==nan false"), sess.reveal(qnan.equal(qnan), PUBLIC).value() == false);
}

// ---------------------------------------------------------------------------
// Zero signs. Under round-to-nearest an exact zero sum is +0, and -0 only when
// both addends are -0; 1 / (x - x) shows the sign as an infinity. fma is
// round(round(a*b) + c), so its addends are the rounded product and c.
// ---------------------------------------------------------------------------
template <int W>
static void zero_signs(const char* tag) {
  using F = Float_T<Ctx, W>;
  using host_t = typename FloatTraits<W>::host_t;
  ClearSession sess;
  auto C = [&](host_t v) { return sess.input<F>(ALICE, v); };
  auto name = [&](const char* op) {
    static char buf[64]; std::snprintf(buf, sizeof buf, "%s %s", tag, op); return buf;
  };
  auto bits_of = [&](const F& v) { return host_bits<W>(sess.reveal(v, PUBLIC).value()); };
  const uint64_t pz = host_bits<W>((host_t)0.0), nz = host_bits<W>((host_t)-0.0);
  const uint64_t pinf = host_bits<W>(std::numeric_limits<host_t>::infinity());

  F x = C((host_t)2.5), nx = C((host_t)-2.5), one = C((host_t)1.0), mone = C((host_t)-1.0);
  F p0 = C((host_t)0.0), n0 = C((host_t)-0.0);
  check_eq<uint64_t>(name("x - x"), bits_of(x - x), pz);
  check_eq<uint64_t>(name("-x - -x"), bits_of(nx - nx), pz);
  check_eq<uint64_t>(name("x + -x"), bits_of(x + nx), pz);
  check_eq<uint64_t>(name("-x + x"), bits_of(nx + x), pz);
  check_eq<uint64_t>(name("+0 + -0"), bits_of(p0 + n0), pz);
  check_eq<uint64_t>(name("-0 + +0"), bits_of(n0 + p0), pz);
  check_eq<uint64_t>(name("-0 + -0"), bits_of(n0 + n0), nz);
  check_eq<uint64_t>(name("+0 - +0"), bits_of(p0 - p0), pz);
  check_eq<uint64_t>(name("-0 - +0"), bits_of(n0 - p0), nz);
  check_eq<uint64_t>(name("fma(1, 1, -1)"), bits_of(one.fma(one, mone)), pz);
  check_eq<uint64_t>(name("fma(-1, 1, 1)"), bits_of(mone.fma(one, one)), pz);
  check_eq<uint64_t>(name("fma(-0, 1, -0)"), bits_of(n0.fma(one, n0)), nz);
  check_eq<uint64_t>(name("1 / (x - x)"), bits_of(one / (x - x)), pinf);

  // Random finite x, subnormals included: x - x and x + (-x) are +0.
  std::mt19937_64 rng(0x5eedULL + W);
  const uint64_t mask = W == 64 ? ~0ULL : ((1ULL << W) - 1);
  int bad = 0;
  for (int i = 0; i < 200; ++i) {
    host_t v = FloatTraits<W>::from_bits(rng() & mask);
    if (!std::isfinite((double)v)) continue;
    F a = C(v), b = C(-v);
    if (bits_of(a - a) != pz || bits_of(a + b) != pz) ++bad;
  }
  check(name("random x - x and x + -x"), bad == 0);
}

// ---------------------------------------------------------------------------
// encode/decode round-trip of the IEEE pattern (the clear codec) at each width.
// ---------------------------------------------------------------------------
template <int W>
static void codec_roundtrip(const char* tag) {
  using F = Float_T<Ctx, W>;
  using host_t = typename FloatTraits<W>::host_t;
  std::mt19937_64 rng(0xDEC0DEULL + (uint64_t)W);
  std::uniform_real_distribution<double> d(-1000.0, 1000.0);
  int bad = 0;
  for (int i = 0; i < 256; ++i) {
    host_t v = (host_t)d(rng);
    auto e = F::encode(v);
    bool bb[W]; for (int i2 = 0; i2 < W; ++i2) bb[i2] = e[i2];
    // decode reproduces the width's representable value of v exactly.
    if (host_bits<W>(F::decode(bb)) != host_bits<W>(v)) ++bad;
  }
  char nm[64]; std::snprintf(nm, sizeof nm, "%s encode/decode", tag);
  check(nm, bad == 0);
}

int main() {
  example();

  width_examples<16>("fp16");
  width_examples<32>("fp32");
  width_examples<64>("fp64");

  random_sweep<32>("fp32", 2000);
  random_sweep<64>("fp64", 2000);
  random_sweep<16>("fp16", 1000);

  compare_sweep<32>("fp32", 2000);
  compare_sweep<64>("fp64", 2000);
  compare_sweep<16>("fp16", 1000);

  boundary_cases<16>("fp16");
  boundary_cases<32>("fp32");
  boundary_cases<64>("fp64");

  zero_signs<16>("fp16");
  zero_signs<32>("fp32");
  zero_signs<64>("fp64");

  codec_roundtrip<16>("fp16");
  codec_roundtrip<32>("fp32");
  codec_roundtrip<64>("fp64");

  printf("test_float: %s\n", g_fail ? "FAILED" : "PASS");
  return g_fail ? 1 : 0;
}
