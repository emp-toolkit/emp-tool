#ifndef EMP_CONTEXT_DIGEST_H__
#define EMP_CONTEXT_DIGEST_H__

// DigestCtx — fold the gate stream (op + operands + output id) into a running
// hash. Two runs of a deterministic kernel/replay produce the same digest; a
// reordered or nondeterministic stream does not. Paired with digest_program(),
// which hashes a stored BooleanProgram by the SAME scheme — so a streamed digest
// (digest_source) can be compared against a recorded/stored program, the
// unambiguous target for replay-determinism checks. The two MUST stay in sync.

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/ir/program.h"
#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstdint>
#include <vector>

namespace emp {

// Wire = wire id; mirrors RecordCtx's numbering so the digests match.
struct DigestCtx {
    using Wire = uint32_t;
    uint64_t digest = 1469598103934665603ull;   // FNV-1a offset basis
    uint32_t next_id = 0;
    int64_t c0 = -1, c1 = -1;                    // dedup consts (mirror RecordCtx)
    void mix_(uint64_t x) { digest = (digest ^ x) * 1099511628211ull; }
    // Reserve input wires [0, n) so gate outputs start at n — the SAME logical
    // numbering as RecordCtx / replay, which the digest must match to detect
    // nondeterministic replay. Mixes num_inputs (input reservation is part of the
    // digest). Call before any gate.
    Wire external_input(size_t n) {
        expecting(n != 0, "DigestCtx::external_input: zero-width argument");
        expecting(n <= static_cast<size_t>(UINT32_MAX - next_id),
                  "DigestCtx::external_input: wire id overflow");
        Wire base = next_id; next_id += (uint32_t)n; mix_(0xE); mix_((uint64_t)n); return base;
    }
    Wire emit_(uint64_t op, uint64_t a, uint64_t b) {
        expecting(next_id != UINT32_MAX, "DigestCtx: wire id overflow");
        uint32_t o = next_id++;
        mix_(op); mix_(a); mix_(b); mix_(o);
        return o;
    }
    // Dedup const0/const1 and mix the gate only on first emission (mirrors
    // RecordCtx) so a streamed digest matches the recorded program's digest.
    Wire public_bit(bool v) {
        int64_t& c = v ? c1 : c0;
        if (c < 0) c = (int64_t)emit_(3 + (v ? 1 : 0), 0, 0);
        return (Wire)c;
    }
    Wire and_gate(Wire a, Wire b) { return emit_(0, a, b); }
    Wire xor_gate(Wire a, Wire b) { return emit_(1, a, b); }
    Wire not_gate(Wire a)         { return emit_(2, a, 0); }
};

static_assert(BooleanContext<DigestCtx>);

// Canonical replay digest. digest_program(p) hashes num_inputs + the gate stream
// exactly as a DigestCtx replay of the same source does, so a streamed digest
// (digest_source) can be compared against a recorded/stored program — the
// unambiguous target for replay-determinism checks.
inline uint64_t digest_program(const circuit::BooleanProgram& p) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    mix(0xE); mix((uint64_t)p.num_inputs);                 // matches external_input(num_inputs)
    for (const circuit::Gate& g : p.gates) {
        // Defaultless switch: the digest codes are part of the hash scheme (they
        // must keep matching DigestCtx::emit_), so a new Op fails the build here
        // instead of silently hashing as some existing code.
        uint64_t op = 0;
        switch (g.op) {
            case circuit::Op::And:    op = 0; break;
            case circuit::Op::Xor:    op = 1; break;
            case circuit::Op::Not:    op = 2; break;
            case circuit::Op::Const0: op = 3; break;
            case circuit::Op::Const1: op = 4; break;
        }
        mix(op); mix(g.in0); mix(g.in1); mix(g.out);
    }
    return h;
}

// Digest a pure circuit body by replaying it through a DigestCtx. `body` is
// callable as body(DigestCtx&, const std::vector<uint32_t>& input_wires) and runs
// gates on the context. Equals digest_program() of the program that body produces
// under RecordCtx.
template <class Body>
inline uint64_t digest_source(uint32_t num_inputs, Body&& body) {
    DigestCtx d;
    uint32_t base = d.external_input((size_t)num_inputs);
    std::vector<uint32_t> in(num_inputs);
    for (uint32_t i = 0; i < num_inputs; ++i) in[i] = base + i;
    body(d, in);
    return d.digest;
}

}  // namespace emp
#endif  // EMP_CONTEXT_DIGEST_H__
