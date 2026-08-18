// Cross-context test for the BooleanContext contract (emp-tool/ir/context/context.h):
// one templated kernel run through Record/Count/Clear/Digest/Backend contexts and
// the value-return replay bridge — all must agree. C++20.

#include "emp-tool/ir/context/context.h"
#include "emp-tool/ir/artifact.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

using namespace emp;
namespace ckt = emp::circuit;

// A small structured kernel (the "keep templated" shape): 16-bit ripple-carry add.
template <class Ctx>
static void add16(Ctx& ctx, const typename Ctx::Wire a[16],
                  const typename Ctx::Wire b[16], typename Ctx::Wire out[16]) {
    using W = typename Ctx::Wire;
    W carry = ctx.public_bit(false);
    for (int i = 0; i < 16; ++i) {
        W axb = ctx.xor_gate(a[i], b[i]);
        out[i] = ctx.xor_gate(axb, carry);
        W t  = ctx.and_gate(axb, carry);
        W ab = ctx.and_gate(a[i], b[i]);
        carry = ctx.xor_gate(ab, t);          // ab, t disjoint -> OR == XOR
    }
}

static int bad = 0;
static void check(const char* what, bool ok) {
    if (!ok) { printf("  [FAIL] %s\n", what); ++bad; }
}

// A kernel that REUSES a constant (public_bit(false) twice). RecordCtx and
// DigestCtx both have Wire = uint32_t, so one body serves both. Used to
// check digest_gate_stream(recorded) == digest_source(same body).
template <class Ctx>
static void const_reuse_kernel(Ctx& ctx, const std::vector<uint32_t>& in, std::vector<uint32_t>& out) {
    auto z1 = ctx.public_bit(false);
    auto a  = ctx.and_gate(in[0], z1);
    auto z2 = ctx.public_bit(false);     // reuse const0 (must dedup to same id)
    auto b  = ctx.xor_gate(in[1], z2);
    out = { ctx.and_gate(a, b) };
}

int main() {
    const uint16_t A = 0xBEEF, B = 0x1234;
    const uint16_t REF = (uint16_t)(A + B);

    // --- record the canonical circuit ---
    RecordCtx rc;
    uint32_t base = rc.external_input(32);
    std::array<uint32_t, 16> ra{}, rb{}, ro{};
    for (int i = 0; i < 16; ++i) { ra[i] = base + i; rb[i] = base + 16 + i; }
    add16(rc, ra.data(), rb.data(), ro.data());
    ckt::BooleanProgram prog = rc.finish(std::span<const uint32_t>(ro.data(), 16));  // validates
    check("record: dims", prog.num_inputs == 32 && prog.outputs.size() == 16);

    // --- canonical CircuitArtifact (program + signature) ---
    {
        ckt::CircuitArtifact art{prog, ckt::CircuitSignature{{16, 16}, 16}};
        ckt::validate_artifact(art);   // error()s on structural/signature mismatch
        check("artifact: signature", art.signature.total_input_bits() == 32 &&
                                      art.signature.return_width == 16);
    }

    uint64_t pa = 0, px = 0, pn = 0, pc = 0;
    for (const auto& g : prog.gates)
        (g.op == ckt::Op::And ? pa : g.op == ckt::Op::Xor ? px
         : g.op == ckt::Op::Not ? pn : pc)++;

    // --- CountCtx: a templated run must match the recorded gate counts ---
    {
        CountCtx cc;
        std::array<uint8_t, 16> a{}, b{}, o{};
        add16(cc, a.data(), b.data(), o.data());
        check("count: AND", cc.ands == pa);
        check("count: XOR", cc.xors == px);
        check("count: NOT", cc.nots == pn);
        check("count: CONST", cc.consts == pc);
    }

    auto bits16 = [](uint16_t v, std::array<uint8_t, 16>& o) {
        for (int i = 0; i < 16; ++i) o[i] = (v >> i) & 1;
    };
    auto recon16 = [](auto&& o) {
        uint16_t v = 0; for (int i = 0; i < 16; ++i) v |= (uint16_t)(o[i] & 1) << i; return v;
    };
    std::array<uint8_t, 16> a8{}, b8{}; bits16(A, a8); bits16(B, b8);

    // --- ClearCtx (static) direct run ---
    uint16_t clear_res = 0;
    {
        ClearCtx cx;
        std::array<uint8_t, 16> o{};
        add16(cx, a8.data(), b8.data(), o.data());
        clear_res = recon16(o);
        check("clear: a+b", clear_res == REF);
    }

    // --- value-return replay bridge over the recorded program ---
    {
        ClearCtx cx;
        std::array<uint8_t, 32> in{};
        for (int i = 0; i < 16; ++i) { in[i] = a8[i]; in[16 + i] = b8[i]; }
        std::vector<uint8_t> o = execute_program(cx, prog,
            std::span<const uint8_t>(in.data(), 32));
        check("bridge: outputs", o.size() == 16);
        check("bridge == clear", recon16(o) == clear_res);
    }

    // --- DigestCtx: faithful wire numbering (inputs [0,n), gates from n);
    //     deterministic replay yields a stable digest ---
    {
        auto digest_add16 = [](DigestCtx& d) {
            uint32_t in = d.external_input(32);              // reserve inputs [0,32)
            std::array<uint32_t, 16> a{}, b{}, o{};
            for (int i = 0; i < 16; ++i) { a[i] = in + i; b[i] = in + 16 + i; }
            add16(d, a.data(), b.data(), o.data());
        };
        DigestCtx d1, d2;
        digest_add16(d1);
        digest_add16(d2);
        const uint64_t v1 = d1.value(), v2 = d2.value();
        check("digest: deterministic", v1 == v2);
        check("digest: nonzero", v1 != 0);
    }

    // --- streamed gate digest: digest_gate_stream(recorded) equals the same
    //     source replay, including const dedup ---
    {
        RecordCtx rc;
        uint32_t base = rc.external_input(2);
        std::vector<uint32_t> rin = {base, base + 1}, rout;
        const_reuse_kernel(rc, rin, rout);
        ckt::BooleanProgram prog = rc.finish(std::span<const uint32_t>(rout.data(), rout.size()));
        uint64_t ds = digest_source(2, [](DigestCtx& d, const std::vector<uint32_t>& in) {
            std::vector<uint32_t> o; const_reuse_kernel(d, in, o);
        });
        check("digest_gate_stream == digest_source (const reuse)",
              digest_gate_stream(prog) == ds);
    }

    // Multiple argument reservations are one aggregate input prefix in the IR.
    {
        RecordCtx rc;
        uint32_t a = rc.external_input(1);
        uint32_t b = rc.external_input(1);
        uint32_t o = rc.xor_gate(a, b);
        ckt::BooleanProgram prog = rc.finish(std::span<const uint32_t>(&o, 1));

        DigestCtx dc;
        uint32_t da = dc.external_input(1);
        uint32_t db = dc.external_input(1);
        dc.xor_gate(da, db);
        check("digest: split input reservations match aggregate program prefix",
              dc.value() == digest_gate_stream(prog));
    }

    // A nullary source reserves no input window but still has a canonical prefix.
    {
        RecordCtx rc;
        uint32_t one = rc.public_bit(true);
        ckt::BooleanProgram prog = rc.finish(std::span<const uint32_t>(&one, 1));
        uint64_t ds = digest_source(0, [](DigestCtx& d, const std::vector<uint32_t>& in) {
            check("digest: nullary input vector", in.empty());
            d.public_bit(true);
        });
        check("digest: nullary source matches gate stream",
              ds == digest_gate_stream(prog));
    }

    // value() seals an input-only trace even when no gate triggered sealing.
    {
        ckt::BooleanProgram input_only;
        input_only.num_wires = 2;
        input_only.num_inputs = 2;
        input_only.outputs = {0, 1};
        ckt::validate_program(input_only);

        DigestCtx dc;
        dc.external_input(2);
        check("digest: zero-gate trace finalizes its input prefix",
              dc.value() == digest_gate_stream(input_only));
    }

    printf("test_context: %s\n", bad ? "FAILED" : "all contexts agree — PASS");
    return bad ? 1 : 0;
}
