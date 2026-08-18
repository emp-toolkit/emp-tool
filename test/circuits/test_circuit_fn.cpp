// Cross-context portability of the BooleanContext circuit frontend: compile a
// circuit ONCE (through a RecordCtx) into a context-free Circuit, then run
// the SAME Circuit on Ctx. Exercises both body forms, a nullary
// circuit, the .constant() sugar, and an fp32 IR-replay body. The 32-bit adder
// is the size-optimal 31-AND kernel; recording is deterministic. C++20.

#include "emp-tool/emp-tool.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/ir/context/context.h"
#include "emp-tool/circuits/typed.h"
#include "emp-tool/circuits/frontend/circuit_fn.h"
#include "emp-tool/circuits/frontend/rec.h"
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>

using namespace emp;
using Ctx = ClearSession::ctx_t;
namespace cf = emp::frontend;

static int bad = 0;
static void chk(const char* what, bool ok) { if (!ok) { printf("  [FAIL] %s\n", what); ++bad; } }

template <class F>
static bool dies(F&& f) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        (void)std::freopen("/dev/null", "w", stderr);
        f();
        std::_Exit(0);
    }
    int status = 0;
    return waitpid(pid, &status, 0) == pid &&
           (!WIFEXITED(status) || WEXITSTATUS(status) != 0);
}

struct CategorySensitive {
    template <class V>
    V operator()(V& v) const { return v.constant(0u); }

    template <class V>
        requires (!std::is_lvalue_reference_v<V>)
    std::decay_t<V> operator()(V&& v) const { return v.constant(1u); }
};

// Decode a value evaluated on Ctx (Wire == uint8_t, 0/1) via its codec.
template <class V> static auto clear_of(const V& v) {
    constexpr int W = V::width();
    typename V::Wire wires[W]; v.pack_wires(wires);
    bool bits[W]; for (int i = 0; i < W; ++i) bits[i] = (bool)(wires[i] & 1);
    return V::decode(bits);
}

static uint64_t count_and(const circuit::BooleanProgram& p) {
    uint64_t n = 0; for (const auto& g : p.gates) if (g.op == circuit::Op::And) ++n; return n;
}

int main() {
    Ctx cx;

    // 1) compile once: 32-bit add (implicit-context form).
    auto add = [](auto a, auto b) { return a + b; };
    auto c = cf::compile<rec::UInt<32>, rec::UInt<32>>(add);
    chk("add: dims", c.program().num_inputs == 64 && c.program().outputs.size() == 32);
    chk("add: AND == 31 (size-optimal adder)", count_and(c.program()) == 31);

    // run the SAME compiled circuit on Ctx, two input sets.
    auto run_add = [&](uint32_t A, uint32_t B) {
        auto x = UInt_T<Ctx, 32>::constant(cx, A);
        auto y = UInt_T<Ctx, 32>::constant(cx, B);
        return (uint32_t)clear_of(cf::run(cx, c, x, y));
    };
    chk("add run 1", run_add(12345678u, 87654321u) == (uint32_t)(12345678u + 87654321u));
    chk("add run 2", run_add(0xDEADBEEFu, 0x12345678u) == (uint32_t)(0xDEADBEEFu + 0x12345678u));

    // recording is deterministic: recompiling yields the identical program.
    auto c2 = cf::compile<rec::UInt<32>, rec::UInt<32>>(add);
    chk("add: deterministic digest", digest_program(c.program()) == digest_program(c2.program()));

    // 2) explicit-context form: a*b + 1 (constant via ctx, no anchor needed).
    auto mul_plus1 = [](auto& ctx, auto a, auto b) {
        using C = std::remove_reference_t<decltype(ctx)>;
        return a * b + UInt_T<C, 32>::constant(ctx, 1);
    };
    auto cmp = cf::compile<rec::UInt<32>, rec::UInt<32>>(mul_plus1);
    {
        uint32_t A = 123456u, B = 789u;
        auto x = UInt_T<Ctx, 32>::constant(cx, A);
        auto y = UInt_T<Ctx, 32>::constant(cx, B);
        chk("explicit-ctx a*b+1", (uint32_t)clear_of(cf::run(cx, cmp, x, y)) == (uint32_t)(A * B + 1));
    }

    // 3) nullary circuit (explicit-context only): a public constant.
    auto kc = cf::compile<>([](auto& ctx) {
        using C = std::remove_reference_t<decltype(ctx)>;
        return UInt_T<C, 16>::constant(ctx, 4242);
    });
    chk("nullary const", (uint32_t)clear_of(cf::run(cx, kc)) == 4242u);

    // 4) .constant() sugar (implicit form): a + a.constant(7).
    auto add7 = [](auto a) { return a + a.constant(7); };
    auto ck = cf::compile<rec::UInt<32>>(add7);
    {
        uint32_t A = 1000u;
        auto x = UInt_T<Ctx, 32>::constant(cx, A);
        chk("implicit .constant", (uint32_t)clear_of(cf::run(cx, ck, x)) == A + 7u);
    }

    // 5) fp32 IR-replay body — compile once, run on Ctx (clear outputs).
    auto fadd = [](auto a, auto b) { return a + b; };
    auto cfp = cf::compile<rec::Float<32>, rec::Float<32>>(fadd);
    {
        auto x = Float_T<Ctx, 32>::constant(cx, 1.5f);
        auto y = Float_T<Ctx, 32>::constant(cx, 2.25f);
        chk("fp32 add (compiled replay)", clear_of(cf::run(cx, cfp, x, y)) == 3.75f);
    }

    // 6) live run (implicit form) agrees with compiled run.
    {
        auto x = UInt_T<Ctx, 32>::constant(cx, 5u);
        auto y = UInt_T<Ctx, 32>::constant(cx, 9u);
        chk("live run", (uint32_t)clear_of(cf::run(add, x, y)) == 14u);
    }

    // Live and compiled bodies both receive owned, decayed argument values.
    // An lvalue/rvalue-overloaded body therefore cannot describe two circuits.
    {
        CategorySensitive body;
        auto category = cf::compile<rec::UInt<32>>(body);
        auto x = UInt_T<Ctx, 32>::constant(cx, 9u);
        chk("category: compiled uses by-value invocation",
            (uint32_t)clear_of(cf::run(cx, category, x)) == 1u);
        chk("category: live matches compiled invocation",
            (uint32_t)clear_of(cf::run(body, x)) == 1u);
    }

    // Context-instance checks are public-boundary checks, not per-gate checks,
    // and remain active in Release builds.
    {
        Ctx other;
        auto x = UInt_T<Ctx, 32>::constant(cx, 1u);
        auto y = UInt_T<Ctx, 32>::constant(cx, 2u);
        auto foreign = UInt_T<Ctx, 32>::constant(other, 3u);
        chk("compiled run rejects foreign argument",
            dies([&] { (void)cf::run(cx, c, x, foreign); }));
        chk("live run rejects foreign argument",
            dies([&] { (void)cf::run(add, x, foreign); }));
        chk("live run rejects foreign return",
            dies([&] {
                auto returns_foreign = [&](auto) { return foreign; };
                (void)cf::run(returns_foreign, y);
            }));

        UInt_T<Ctx, 32> uninitialized;
        chk("live run rejects uninitialized first argument",
            dies([&] { (void)cf::run([](auto a) { return a; }, uninitialized); }));

        RecordCtx foreign_rc;
        std::array<RecordCtx::Wire, 32> wires{};
        RecordCtx::Wire base = foreign_rc.external_input(wires.size());
        for (std::size_t i = 0; i < wires.size(); ++i)
            wires[i] = base + static_cast<RecordCtx::Wire>(i);
        auto foreign_record = rec::UInt<32>::from_wires(foreign_rc, wires.data());
        chk("compile rejects foreign return",
            dies([&] {
                (void)cf::compile<rec::UInt<32>>(
                    [&](auto) { return foreign_record; });
            }));
    }

    // 7) BitVec_T circuit (bit-vector value): nibble swap, compiled once, run on Ctx.
    {
        auto swap = [](auto b) { return b.template slice<4, 8>().concat(b.template slice<0, 4>()); };
        auto cbits = cf::compile<rec::BitVec<8>>(swap);
        std::array<bool, 8> v{}; for (int i = 0; i < 8; ++i) v[i] = (0xB5u >> i) & 1;
        auto out = cf::run(cx, cbits, BitVec_T<Ctx, 8>::constant(cx, v));
        uint32_t got = 0; for (int i = 0; i < 8; ++i) if (out.w[i] & 1) got |= (1u << i);
        chk("BitVec_T nibble-swap circuit", got == 0x5Bu);   // 0xB5 -> 0x5B
    }

    // 8) wide-integer circuit args: UInt_T<Ctx,128> has no 64-bit clear codec, so
    // it is a WireBundle but not a WireValue — and the frontend needs only the
    // bundle. Compile a 128-bit add and check the wire-level result against a
    // limb-wise model (no decode at this width; that's the point).
    {
        static_assert(emp::WireBundle<UInt_T<Ctx, 128>> && !emp::WireValue<UInt_T<Ctx, 128>>);
        auto wadd = cf::compile<rec::UInt<128>, rec::UInt<128>>([](auto a, auto b) { return a + b; });
        auto mk = [&](uint64_t lo, uint64_t hi) {
            Ctx::Wire w[128];
            for (int i = 0; i < 64; ++i)  w[i]      = (Ctx::Wire)((lo >> i) & 1);
            for (int i = 0; i < 64; ++i)  w[64 + i] = (Ctx::Wire)((hi >> i) & 1);
            return UInt_T<Ctx, 128>::from_wires(cx, w);
        };
        // Carry must propagate across the limb boundary.
        auto r = cf::run(cx, wadd, mk(~0ull, 1ull), mk(1ull, 2ull));   // (2^64-1 + 2^65) + (1 + 2^65)
        Ctx::Wire ow[128]; r.pack_wires(ow);
        uint64_t lo = 0, hi = 0;
        for (int i = 0; i < 64; ++i) { lo |= (uint64_t)(ow[i] & 1) << i; hi |= (uint64_t)(ow[64 + i] & 1) << i; }
        chk("128-bit add: low limb", lo == 0ull);          // (2^64-1) + 1 = 2^64 -> carry out
        chk("128-bit add: high limb", hi == 4ull);         // 1 + 2 + carry = 4
    }

    printf("test_circuit_fn: %s\n", bad ? "FAILED" : "compile-once / run-anywhere — PASS");
    return bad ? 1 : 0;
}
