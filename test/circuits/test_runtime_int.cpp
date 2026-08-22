// Fixed/dynamic integer parity and runtime-width session boundaries.
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/ir/context/clear.h"
#include "emp-tool/ir/context/count.h"
#include "emp-tool/ir/context/digest.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>
using namespace emp;
using Ctx = ClearSession::ctx_t;

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }
static uint64_t rdu(const DynamicUInt_T<Ctx>& u) {
  uint64_t v = 0; for (int i = 0; i < u.width(); ++i) v |= (uint64_t)(u.data()[i] & 1) << i; return v;
}
static int64_t rds(const DynamicInt_T<Ctx>& x) {
  uint64_t v = 0; int N = x.width();
  for (int i = 0; i < N; ++i) v |= (uint64_t)(x.data()[i] & 1) << i;
  if (N < 64 && ((v >> (N - 1)) & 1)) v |= ~((uint64_t(1) << N) - 1);
  return (int64_t)v;
}

template <int N, bool Dynamic, class Context>
using UnsignedValue = std::conditional_t<Dynamic,
                                         DynamicUInt_T<Context>,
                                         UInt_T<Context, N>>;

template <int N, bool Dynamic, class Context>
using SignedValue = std::conditional_t<Dynamic,
                                       DynamicInt_T<Context>,
                                       Int_T<Context, N>>;

template <int N, bool Dynamic, class Context>
static UnsignedValue<N, Dynamic, Context> unsigned_constant(Context& ctx,
                                                             uint64_t value) {
  if constexpr (Dynamic)
    return DynamicUInt_T<Context>::constant(ctx, N, value);
  else
    return UInt_T<Context, N>::constant(ctx, value);
}

template <int N, bool Dynamic, class Context>
static SignedValue<N, Dynamic, Context> signed_constant(Context& ctx,
                                                         int64_t value) {
  if constexpr (Dynamic)
    return DynamicInt_T<Context>::constant(ctx, N, value);
  else
    return Int_T<Context, N>::constant(ctx, value);
}

template <class Value>
static void append_wires(std::vector<typename Value::Wire>& trace,
                         const Value& value) {
  for (int i = 0; i < value.width(); ++i)
    trace.push_back(value.data()[(std::size_t)i]);
}

template <int N, bool Dynamic, BooleanContext Context>
static std::vector<typename Context::Wire> exercise_unsigned(Context& ctx) {
  using U = UnsignedValue<N, Dynamic, Context>;
  U a, b;
  Bit_T<Context> select_b;
  if constexpr (std::is_same_v<Context, DigestCtx>) {
    std::vector<typename Context::Wire> inputs((std::size_t)(2 * N + 1));
    auto base = ctx.external_input(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i)
      inputs[i] = base + (typename Context::Wire)i;
    if constexpr (Dynamic) {
      a = U::from_wires(ctx, inputs.data(), N);
      b = U::from_wires(ctx, inputs.data() + N, N);
    } else {
      a = U::from_wires(ctx, inputs.data());
      b = U::from_wires(ctx, inputs.data() + N);
    }
    select_b = Bit_T<Context>::from_wires(ctx, inputs.data() + 2 * N);
  } else {
    a = unsigned_constant<N, Dynamic>(ctx, 0xD39A7C4E215608BAull);
    b = unsigned_constant<N, Dynamic>(ctx, 0x13579BDF02468AC5ull);
    select_b = Bit_T<Context>::constant(ctx, true);
  }
  std::vector<typename Context::Wire> trace;

  append_wires(trace, a & b);
  append_wires(trace, a | b);
  append_wires(trace, a ^ b);
  append_wires(trace, ~a);
  append_wires(trace, a.select(select_b, b));

  constexpr int shifts[] = {0, 1, N - 1, N, N + 1};
  for (int shift : shifts) {
    append_wires(trace, a << shift);
    append_wires(trace, a >> shift);
    append_wires(trace, a.rotl(shift));
    append_wires(trace, a.rotr(shift));
  }
  return trace;
}

template <int N, bool Dynamic, BooleanContext Context>
static std::vector<typename Context::Wire> exercise_signed(Context& ctx) {
  using I = SignedValue<N, Dynamic, Context>;
  I a, b;
  Bit_T<Context> select_b;
  if constexpr (std::is_same_v<Context, DigestCtx>) {
    std::vector<typename Context::Wire> inputs((std::size_t)(2 * N + 1));
    auto base = ctx.external_input(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i)
      inputs[i] = base + (typename Context::Wire)i;
    if constexpr (Dynamic) {
      a = I::from_wires(ctx, inputs.data(), N);
      b = I::from_wires(ctx, inputs.data() + N, N);
    } else {
      a = I::from_wires(ctx, inputs.data());
      b = I::from_wires(ctx, inputs.data() + N);
    }
    select_b = Bit_T<Context>::from_wires(ctx, inputs.data() + 2 * N);
  } else {
    a = signed_constant<N, Dynamic>(ctx, -37);
    b = signed_constant<N, Dynamic>(ctx, 5);
    select_b = Bit_T<Context>::constant(ctx, true);
  }
  std::vector<typename Context::Wire> trace;

  append_wires(trace, a & b);
  append_wires(trace, a | b);
  append_wires(trace, a ^ b);
  append_wires(trace, ~a);
  append_wires(trace, a.select(select_b, b));

  constexpr int shifts[] = {0, 1, N - 1, N, N + 1};
  for (int shift : shifts) {
    append_wires(trace, a << shift);
    append_wires(trace, a >> shift);
  }
  return trace;
}

static void chk_width(const char* what, int width, bool ok) {
  if (!ok) {
    printf("  [FAIL] %s at width %d\n", what, width);
    ++bad;
  }
}

template <int N>
static void check_value_parity() {
  ClearCtx fixed_unsigned_ctx, dynamic_unsigned_ctx;
  chk_width("unsigned fixed/dynamic values", N,
            exercise_unsigned<N, false>(fixed_unsigned_ctx) ==
                exercise_unsigned<N, true>(dynamic_unsigned_ctx));

  ClearCtx fixed_signed_ctx, dynamic_signed_ctx;
  chk_width("signed fixed/dynamic values", N,
            exercise_signed<N, false>(fixed_signed_ctx) ==
                exercise_signed<N, true>(dynamic_signed_ctx));
}

struct GateCounts {
  uint64_t ands, xors, nots, consts;
  bool operator==(const GateCounts&) const = default;
};

static GateCounts counts(const CountCtx& ctx) {
  return {ctx.ands, ctx.xors, ctx.nots, ctx.consts};
}

template <int N>
static void check_trace_parity() {
  CountCtx fixed_unsigned_count, dynamic_unsigned_count;
  (void)exercise_unsigned<N, false>(fixed_unsigned_count);
  (void)exercise_unsigned<N, true>(dynamic_unsigned_count);
  chk_width("unsigned fixed/dynamic gate counts", N,
            counts(fixed_unsigned_count) == counts(dynamic_unsigned_count));

  CountCtx fixed_signed_count, dynamic_signed_count;
  (void)exercise_signed<N, false>(fixed_signed_count);
  (void)exercise_signed<N, true>(dynamic_signed_count);
  chk_width("signed fixed/dynamic gate counts", N,
            counts(fixed_signed_count) == counts(dynamic_signed_count));

  DigestCtx fixed_unsigned_digest, dynamic_unsigned_digest;
  auto fixed_unsigned_wires =
      exercise_unsigned<N, false>(fixed_unsigned_digest);
  auto dynamic_unsigned_wires =
      exercise_unsigned<N, true>(dynamic_unsigned_digest);
  chk_width("unsigned fixed/dynamic gate digest", N,
            fixed_unsigned_digest.value() == dynamic_unsigned_digest.value());
  chk_width("unsigned fixed/dynamic output wires", N,
            fixed_unsigned_wires == dynamic_unsigned_wires);

  DigestCtx fixed_signed_digest, dynamic_signed_digest;
  auto fixed_signed_wires = exercise_signed<N, false>(fixed_signed_digest);
  auto dynamic_signed_wires = exercise_signed<N, true>(dynamic_signed_digest);
  chk_width("signed fixed/dynamic gate digest", N,
            fixed_signed_digest.value() == dynamic_signed_digest.value());
  chk_width("signed fixed/dynamic output wires", N,
            fixed_signed_wires == dynamic_signed_wires);
}

static void fixed_dynamic_parity() {
  check_value_parity<1>();
  check_value_parity<2>();
  check_value_parity<7>();
  check_value_parity<8>();
  check_value_parity<31>();
  check_value_parity<32>();
  check_value_parity<63>();
  check_value_parity<64>();
  check_value_parity<65>();

  check_trace_parity<1>();
  check_trace_parity<8>();
  check_trace_parity<32>();
  check_trace_parity<65>();
}

// ---- runtime-width session flow ------------------------------------------
static void example() {
  ClearSession sess;
  using DU = DynamicUInt_T<Ctx>;
  using DI = DynamicInt_T<Ctx>;
  DU a = sess.input<DU>(ALICE, 1000u, /*width=*/20);
  DU b = sess.input<DU>(BOB,     24u, 20);
  chk("example u add", sess.reveal(a + b, PUBLIC).value() == 1024u);
  DI s = sess.input<DI>(ALICE, -5, 16);
  chk("example i neg", sess.reveal(-s, PUBLIC).value() == 5);
}

int main() {
  example();
  fixed_dynamic_parity();

  Ctx cx;
  using DU = DynamicUInt_T<Ctx>;
  const uint32_t A = 1234567u, B = 7654321u;
  DU a = DU::constant(cx, 32, A), b = DU::constant(cx, 32, B);

  chk("width", a.width() == 32);
  // arithmetic
  chk("add", rdu(a + b) == (uint32_t)(A + B));
  chk("sub", rdu(a - b) == (uint32_t)(A - B));
  chk("mul", rdu(a * b) == (uint32_t)(A * B));
  chk("div", rdu(b / a) == (B / A));
  chk("mod", rdu(b % a) == (B % A));
  // bitwise
  chk("and", rdu(a & b) == (A & B));
  chk("or",  rdu(a | b) == (A | B));
  chk("xor", rdu(a ^ b) == (A ^ B));
  chk("not", rdu(~a) == (uint32_t)(~A));
  // comparisons
  chk("lt", ((a < b).w & 1) == 1);
  chk("gt", ((b > a).w & 1) == 1);
  chk("eq", ((a == a).w & 1) == 1);
  chk("ne", ((a != b).w & 1) == 1);
  chk("select", rdu(a.select((a < b), b)) == B);
  // public shifts
  chk("shl", rdu(a << 4) == (uint32_t)(A << 4));
  chk("shr", rdu(a >> 4) == (A >> 4));
  chk("rotl", rdu(a.rotl(4)) == (uint32_t)((A << 4) | (A >> 28)));
  chk("rotr", rdu(a.rotr(4)) == (uint32_t)((A >> 4) | (A << 28)));
  // resize up / down
  chk("resize up", rdu(a.resize(48)) == (uint64_t)A);
  chk("resize down", rdu(a.resize(16)) == (A & 0xffff));
  chk("popcount", rdu(a.hamming_weight()) == (uint64_t)__builtin_popcount(A));
  // fixed <-> runtime conversion
  chk("to_fixed", [&]{ auto f = a.to_fixed<32>(); uint64_t v = 0; for (int i = 0; i < 32; ++i) v |= (uint64_t)(f.w[i] & 1) << i; return v == A; }());
  chk("to_dynamic", rdu(UInt_T<Ctx, 32>::constant(cx, A).to_dynamic()) == A);

  // signed runtime
  using DI = DynamicInt_T<Ctx>;
  const int32_t SA = -1234567, SB = 7654321;
  DI sa = DI::constant(cx, 32, SA), sb = DI::constant(cx, 32, SB);
  chk("signed add", rds(sa + sb) == (int32_t)(SA + SB));
  // Reference wraps mod 2^32 (matches the runtime Int's 2's-complement mul);
  // compute it in unsigned to keep the reference itself free of signed overflow.
  chk("signed mul", rds(sa * sb) == (int32_t)((uint32_t)SA * (uint32_t)SB));
  chk("signed div", rds(sb / sa) == (SB / SA));
  chk("signed mod", rds(sb % sa) == (SB % SA));
  chk("signed lt", ((sa < sb).w & 1) == 1);
  chk("neg", rds(-sa) == (int32_t)(-SA));
  chk("arith shr", rds(sa >> 4) == (int32_t)(SA >> 4));
  chk("sext resize", rds(sa.resize(48)) == (int64_t)SA);
  chk("as_unsigned", rdu(sa.as_unsigned()) == (uint32_t)SA);

  // constants wider than 64: unsigned zero-extends, signed sign-extends.
  {
    DU big = DU::constant(cx, 100, 0xFFFFFFFFFFFFFFFFull);
    bool z = true; for (int i = 64; i < 100; ++i) if (big.data()[i] & 1) z = false;
    chk("u const >64 zero-extends", z && (big.data()[0] & 1) && (big.data()[63] & 1));
    DI sbig = DI::constant(cx, 100, -1);
    bool one = true; for (int i = 0; i < 100; ++i) if (!(sbig.data()[i] & 1)) one = false;
    chk("i const >64 sign-extends", one);
  }

  printf("test_runtime_int: %s\n", bad ? "FAILED" : "PASS");
  return bad ? 1 : 0;
}
