// Fixed/dynamic integer parity and runtime-width session boundaries.
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/ir/context/clear.h"
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/runtime/core/constants.h"
#include <cstdint>
#include <cstdio>
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
