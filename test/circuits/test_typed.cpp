// Typed values over a BooleanContext (emp-tool/circuits/typed.h): exercise
// UInt/Int arithmetic+compare and Float arithmetic (IR replay) on
// Ctx vs host, plus a record->replay round-trip. C++20.

#include "emp-tool/emp-tool.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/circuits/typed.h"
#include "emp-tool/ir/context/context.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <type_traits>

using namespace emp;
using Ctx = ClearSession::ctx_t;
namespace ckt = emp::circuit;

// Round-trip guards: rebinding a value to its own context is the identity.
static_assert(std::is_same_v<Bit_T<Ctx>::rebind<Ctx>,         Bit_T<Ctx>>);
static_assert(std::is_same_v<UInt_T<Ctx, 32>::rebind<Ctx>,    UInt_T<Ctx, 32>>);
static_assert(std::is_same_v<Int_T<Ctx, 32>::rebind<Ctx>,     Int_T<Ctx, 32>>);
static_assert(std::is_same_v<Float_T<Ctx, 32>::rebind<Ctx>,   Float_T<Ctx, 32>>);
static_assert(std::is_same_v<BitVec_T<Ctx, 128>::rebind<Ctx>, BitVec_T<Ctx, 128>>);

static int bad = 0;
static void chk(const char* what, bool ok) { if (!ok) { printf("  [FAIL] %s\n", what); ++bad; } }

template <int N, class V> static uint64_t read_bits(const V& v) {
    uint64_t r = 0; for (int i = 0; i < N; ++i) r |= (uint64_t)(v.w[i] & 1) << i; return r;
}

int main() {
    Ctx cx;
    const uint32_t A = 0xDEADBEEF, B = 0x12345678;

    // ---- UInt32 arithmetic / compare vs host ----
    {
        auto a = UInt_T<Ctx, 32>::constant(cx, A);
        auto b = UInt_T<Ctx, 32>::constant(cx, B);
        chk("uint +",  read_bits<32>(a + b) == (uint32_t)(A + B));
        chk("uint -",  read_bits<32>(a - b) == (uint32_t)(A - B));
        chk("uint &",  read_bits<32>(a & b) == (A & B));
        chk("uint ^",  read_bits<32>(a ^ b) == (A ^ B));
        chk("uint |",  read_bits<32>(a | b) == (A | B));
        chk("uint ~",  read_bits<32>(~a) == (uint32_t)(~A));
        chk("uint ==", ((a == a).w & 1) == 1 && ((a == b).w & 1) == 0);
        chk("uint <",  ((b < a).w & 1) == (B < A) && ((a < b).w & 1) == (A < B));
        chk("uint sel", read_bits<32>(a.select(Bit_T<Ctx>::constant(cx, true), b)) == B);
        chk("uint *",  read_bits<32>(a * b) == (uint32_t)(A * B));
        chk("uint /",  read_bits<32>(a / b) == (uint32_t)(A / B));
        chk("uint %",  read_bits<32>(a % b) == (uint32_t)(A % B));
    }

    // ---- Int32: +, -, unary-, signed < ----
    {
        const int32_t X = -1000000, Y = 2000000;
        auto x = Int_T<Ctx, 32>::constant(cx, X);
        auto y = Int_T<Ctx, 32>::constant(cx, Y);
        chk("int +", (int32_t)read_bits<32>(x + y) == (int32_t)(X + Y));
        chk("int -", (int32_t)read_bits<32>(x - y) == (int32_t)(X - Y));
        chk("int neg", (int32_t)read_bits<32>(-x) == -X);
        chk("int < (neg<pos)", ((x < y).w & 1) == 1);
        chk("int < (pos<neg)", ((y < x).w & 1) == 0);
        chk("int *", (int32_t)read_bits<32>(x * y) == (int32_t)((uint32_t)X * (uint32_t)Y));
        chk("int &", (int32_t)read_bits<32>(x & y) == (int32_t)(X & Y));
        chk("int ^", (int32_t)read_bits<32>(x ^ y) == (int32_t)(X ^ Y));
        chk("int |", (int32_t)read_bits<32>(x | y) == (int32_t)(X | Y));
        chk("int ~", (int32_t)read_bits<32>(~x) == (int32_t)(~X));
        chk("int ==", ((x == x).w & 1) == 1 && ((x == y).w & 1) == 0);
        chk("int !=", ((x != y).w & 1) == 1 && ((x != x).w & 1) == 0);
        chk("int <=/>=", ((x <= y).w & 1) == 1 && ((y >= x).w & 1) == 1 && ((y <= x).w & 1) == 0);
        chk("int []", ((uint32_t)(x[0].w & 1)) == ((uint32_t)X & 1));
        chk("int sel", (int32_t)read_bits<32>(x.select(Bit_T<Ctx>::constant(cx, true), y)) == Y);
        // signed div/mod truncate toward zero; remainder takes the dividend's sign
        // (C++ semantics) — check all four sign combinations.
        struct DM { int32_t a, b; };
        for (DM t : {DM{-100, 7}, DM{100, -7}, DM{-100, -7}, DM{100, 7}, DM{7, 100}, DM{-7, -100}}) {
            auto ia = Int_T<Ctx, 32>::constant(cx, t.a);
            auto ib = Int_T<Ctx, 32>::constant(cx, t.b);
            chk("int /", (int32_t)read_bits<32>(ia / ib) == t.a / t.b);
            chk("int %", (int32_t)read_bits<32>(ia % ib) == t.a % t.b);
        }
    }

    // ---- Float32 arithmetic via IR replay through the context ----
    {
        auto bits = [](float f) { uint32_t u; std::memcpy(&u, &f, 4); return (uint64_t)u; };
        auto tofl = [](uint64_t u32) { float f; uint32_t u = (uint32_t)u32; std::memcpy(&f, &u, 4); return f; };
        auto fa = Float_T<Ctx, 32>::from_bits(cx, bits(1.5f));
        auto fb = Float_T<Ctx, 32>::from_bits(cx, bits(2.25f));
        auto f2 = Float_T<Ctx, 32>::from_bits(cx, bits(2.0f));
        auto f4 = Float_T<Ctx, 32>::from_bits(cx, bits(4.0f));
        auto fz = Float_T<Ctx, 32>::from_bits(cx, bits(0.0f));
        auto fn = Float_T<Ctx, 32>::from_bits(cx, bits(-1.5f));
        chk("float +", tofl(read_bits<32>(fa + fb)) == 3.75f);
        chk("float *", tofl(read_bits<32>(fa * fb)) == 3.375f);
        chk("float -", tofl(read_bits<32>(fb - fa)) == 0.75f);
        chk("float /", tofl(read_bits<32>(fb / fa)) == 1.5f);
        chk("float min", tofl(read_bits<32>(fa.min(fb))) == 1.5f);
        chk("float max", tofl(read_bits<32>(fa.max(fb))) == 2.25f);
        chk("float sqr", tofl(read_bits<32>(fa.sqr())) == 2.25f);
        chk("float sqrt", tofl(read_bits<32>(f4.sqrt())) == 2.0f);
        chk("float recip", tofl(read_bits<32>(f2.recip())) == 0.5f);
        chk("float rsqrt", tofl(read_bits<32>(f4.rsqrt())) == 0.5f);
        chk("float fma", tofl(read_bits<32>(fa.fma(fb, f4))) == 1.5f * 2.25f + 4.0f);
        // comparisons -> bit
        chk("float lt", ((fa.less_than(fb)).w & 1) == 1 && ((fb.less_than(fa)).w & 1) == 0);
        chk("float le", ((fa.less_equal(fa)).w & 1) == 1);
        chk("float gt", ((fb.greater_than(fa)).w & 1) == 1);
        chk("float ge", ((fa.greater_equal(fa)).w & 1) == 1);
        chk("float eq", ((fa.equal(fa)).w & 1) == 1 && ((fa.equal(fb)).w & 1) == 0);
        chk("float ne", ((fa.not_equal(fb)).w & 1) == 1);
        // classifiers
        chk("float iszero", ((fz.is_zero()).w & 1) == 1 && ((fa.is_zero()).w & 1) == 0);
        chk("float isinf", ((fa.is_inf()).w & 1) == 0);
        chk("float isnan", ((fa.is_nan()).w & 1) == 0);
        // sign-bit ops + select
        chk("float abs", tofl(read_bits<32>(fn.abs())) == 1.5f);
        chk("float neg", tofl(read_bits<32>(-fa)) == -1.5f);
        chk("float copysign", tofl(read_bits<32>(fa.copysign(fn))) == -1.5f);
        chk("float select T", tofl(read_bits<32>(fa.select(Bit_T<Ctx>::constant(cx, true), fb))) == 2.25f);
        chk("float select F", tofl(read_bits<32>(fa.select(Bit_T<Ctx>::constant(cx, false), fb))) == 1.5f);

        // host clear codec (FloatTraits) + constant()
        const std::array<bool, 32> e = Float_T<Ctx, 32>::encode(3.75f);
        chk("float codec roundtrip", Float_T<Ctx, 32>::decode(e.data()) == 3.75f);
        auto ca = Float_T<Ctx, 32>::constant(cx, 1.5f);
        auto cb = Float_T<Ctx, 32>::constant(cx, 2.25f);
        auto cc = ca + cb;
        bool cbits[32]; for (int i = 0; i < 32; ++i) cbits[i] = cc.w[i] & 1;
        chk("float constant + host decode", Float_T<Ctx, 32>::decode(cbits) == 3.75f);
    }

    // ---- shifts / rotates / slice / concat / extend (pure wiring) ----
    {
        const uint32_t V = 0x12345678u;
        auto u = UInt_T<Ctx, 32>::constant(cx, V);
        chk("uint <<",  read_bits<32>(u << 4)  == (uint32_t)(V << 4));
        chk("uint >>",  read_bits<32>(u >> 4)  == (V >> 4));
        chk("uint <<31", read_bits<32>(u << 31) == (uint32_t)(V << 31));
        chk("uint >>40 (over-shift = 0)", read_bits<32>(u >> 40) == 0u);
        auto rotr8 = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
        auto rotl8 = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
        chk("uint rotr", read_bits<32>(u.rotr(8)) == rotr8(V, 8));
        chk("uint rotl", read_bits<32>(u.rotl(8)) == rotl8(V, 8));
        chk("uint slice<8,24>", read_bits<16>(u.slice<8, 24>()) == (uint16_t)((V >> 8) & 0xFFFF));
        chk("uint trunc<16>",   read_bits<16>(u.trunc<16>())    == (uint16_t)(V & 0xFFFF));
        chk("uint zext<48>",    read_bits<48>(u.zext<48>())     == (uint64_t)V);
        auto lo = UInt_T<Ctx, 16>::constant(cx, 0xBEEFu);
        auto hi = UInt_T<Ctx, 16>::constant(cx, 0xDEADu);
        chk("uint concat", read_bits<32>(lo.concat(hi)) == 0xDEADBEEFu);

        // Int_T: arithmetic >> sign-extends; << logical; sext.
        const int32_t S = -1234567;
        auto si = Int_T<Ctx, 32>::constant(cx, S);
        chk("int <<",  (int32_t)read_bits<32>(si << 3) == (int32_t)((uint32_t)S << 3));
        chk("int >> (arith)", (int32_t)read_bits<32>(si >> 4) == (S >> 4));
        chk("int sext<48>", (read_bits<48>(si.sext<48>()) == ((uint64_t)(int64_t)S & ((1ull << 48) - 1))));
    }

    // ---- BitVec_T<Ctx,N>: bit-vector value (block I/O + assembly) ----
    {
        std::array<bool, 8> v{}; for (int i = 0; i < 8; ++i) v[i] = (0xB5u >> i) & 1;  // 0xB5
        auto b = BitVec_T<Ctx, 8>::constant(cx, v);
        chk("bits []",    (b[0].w & 1) == 1 && (b[1].w & 1) == 0);   // 0xB5 = 1011'0101
        chk("bits as_uint", read_bits<8>(b.as_uint()) == 0xB5u);
        chk("bits constant() sugar", read_bits<8>(b.constant(v).as_uint()) == 0xB5u);   // member sugar returns BitVec_T
        chk("bits slice<0,4>", read_bits<4>(b.slice<0, 4>().as_uint()) == 0x5u);
        std::array<bool, 4> hv{}; for (int i = 0; i < 4; ++i) hv[i] = (0xAu >> i) & 1;
        auto hb = BitVec_T<Ctx, 4>::constant(cx, hv);
        chk("bits concat", read_bits<8>(b.slice<0, 4>().concat(hb).as_uint()) == (uint32_t)((0xA << 4) | 0x5));
        bool eb[8]; auto e = BitVec_T<Ctx, 8>::encode(v); for (int i = 0; i < 8; ++i) eb[i] = e[i];
        chk("bits codec roundtrip", BitVec_T<Ctx, 8>::decode(eb) == v);
    }

    // ---- record a typed UInt circuit, replay it on real inputs ----
    {
        RecordCtx rc;
        uint32_t base = rc.external_input(64);
        uint32_t aw[32], bw[32];
        for (int i = 0; i < 32; ++i) { aw[i] = base + i; bw[i] = base + 32 + i; }
        auto ra = UInt_T<RecordCtx, 32>::from_wires(rc, aw);
        auto rb = UInt_T<RecordCtx, 32>::from_wires(rc, bw);
        auto rsum = ra + rb;
        uint32_t outw[32]; rsum.pack_wires(outw);
        ckt::BooleanProgram prog = rc.finish(std::span<const uint32_t>(outw, 32));

        std::array<uint8_t, 64> in{};
        for (int i = 0; i < 32; ++i) { in[i] = (A >> i) & 1; in[32 + i] = (B >> i) & 1; }
        Ctx cx2;
        std::vector<uint8_t> out = execute_program(cx2, prog, std::span<const uint8_t>(in.data(), 64));
        uint32_t res = 0; for (int i = 0; i < 32; ++i) res |= (uint32_t)(out[i] & 1) << i;
        chk("record/replay uint +", res == (uint32_t)(A + B));
    }

    printf("test_typed: %s\n", bad ? "FAILED" : "typed values over Ctx — PASS");
    return bad ? 1 : 0;
}
