#ifndef EMP_CONTEXT_DIGEST_H__
#define EMP_CONTEXT_DIGEST_H__

// DigestCtx — fold the gate stream (op + operands + output id) into a running
// diagnostic hash. Two runs of a deterministic kernel/replay produce the same
// digest; a reordered or nondeterministic stream does not. Paired with
// digest_gate_stream(), which hashes a stored BooleanProgram by the SAME scheme.
// This 64-bit FNV fold detects accidental drift; it is not a cryptographic hash
// and must not authenticate a program or bind a protocol transcript.

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/ir/program.h"
#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstdint>
#include <vector>

namespace emp {

// Wire = wire id; mirrors RecordCtx's numbering so the digests match.
struct DigestCtx {
    using Wire = uint32_t;
    // Reserve input wires [0, n) so gate outputs start at n. Multiple argument
    // windows are accumulated and framed once as their total width, matching the
    // single num_inputs field recorded by RecordCtx / BooleanProgram.
    Wire external_input(size_t n) {
        expecting(!inputs_closed,
                  "DigestCtx::external_input: called after inputs were sealed");
        expecting(n != 0, "DigestCtx::external_input: zero-width argument");
        expecting(n <= static_cast<size_t>(UINT32_MAX - next_id),
                  "DigestCtx::external_input: wire id overflow");
        Wire base = next_id; next_id += (uint32_t)n; return base;
    }
    void seal_inputs() {
        if (inputs_closed) return;
        mix_(0xE); mix_((uint64_t)next_id);
        inputs_closed = true;
    }
    // Dedup const0/const1 and mix the gate only on first emission (mirrors
    // RecordCtx) so a streamed digest matches the recorded program's digest.
    Wire public_bit(bool v) {
        int64_t& c = v ? c1 : c0;
        if (c < 0)
            c = (int64_t)emit_(v ? circuit::Op::Const1 : circuit::Op::Const0,
                               0, 0);
        return (Wire)c;
    }
    Wire and_gate(Wire a, Wire b) { return emit_(circuit::Op::And, a, b); }
    Wire xor_gate(Wire a, Wire b) { return emit_(circuit::Op::Xor, a, b); }
    Wire not_gate(Wire a)         { return emit_(circuit::Op::Not, a, 0); }

    // Snapshot the completed trace. This seals even a zero-gate input prefix, so
    // callers cannot accidentally observe the raw FNV offset for such a source.
    uint64_t value() { seal_inputs(); return digest_; }

private:
    uint64_t digest_ = 1469598103934665603ull;   // FNV-1a offset basis
    uint32_t next_id = 0;
    int64_t c0 = -1, c1 = -1;                    // dedup consts (mirror RecordCtx)
    bool inputs_closed = false;

    void mix_(uint64_t x) { digest_ = (digest_ ^ x) * 1099511628211ull; }
    Wire emit_(circuit::Op op, uint64_t a, uint64_t b) {
        seal_inputs();
        expecting(next_id != UINT32_MAX, "DigestCtx: wire id overflow");
        uint32_t o = next_id++;
        mix_(static_cast<uint8_t>(op)); mix_(a); mix_(b); mix_(o);
        return o;
    }
};

static_assert(BooleanContext<DigestCtx>);

// Replay-trace digest. This intentionally excludes outputs and storage metadata:
// it answers whether a source emitted the same inputs and gates, not whether two
// complete BooleanProgram values are identical.
inline uint64_t digest_gate_stream(const circuit::BooleanProgram& p) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    mix(0xE); mix((uint64_t)p.num_inputs);
    for (const circuit::Gate& g : p.gates) {
        mix(static_cast<uint8_t>(g.op));
        mix(g.in0); mix(g.in1); mix(g.out);
    }
    return h;
}

// Full structural fingerprint of a BooleanProgram. The domain and version
// distinguish it from the streamed construction trace and version the framing.
// Counts frame every variable sequence; ordered outputs are part of program
// semantics.
inline uint64_t digest_program(const circuit::BooleanProgram& p) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    mix(0x454d505f49525f50ull);   // "EMP_IR_P"
    mix(1);                       // fingerprint format version
    mix(static_cast<uint8_t>(p.wire_reuse));
    mix(p.num_wires);
    mix(p.num_inputs);
    mix((uint64_t)p.gates.size());
    for (const circuit::Gate& g : p.gates) {
        mix(static_cast<uint8_t>(g.op));
        mix(g.in0); mix(g.in1); mix(g.out);
    }
    mix((uint64_t)p.outputs.size());
    for (uint32_t w : p.outputs) mix(w);
    return h;
}

// Digest a pure circuit body by replaying it through a DigestCtx. `body` is
// callable as body(DigestCtx&, const std::vector<uint32_t>& input_wires) and runs
// gates on the context. Equals digest_gate_stream() of the program that body
// produces under RecordCtx.
template <class Body>
inline uint64_t digest_source(uint32_t num_inputs, Body&& body) {
    DigestCtx d;
    uint32_t base = 0;
    if (num_inputs != 0) base = d.external_input((size_t)num_inputs);
    std::vector<uint32_t> in(num_inputs);
    for (uint32_t i = 0; i < num_inputs; ++i) in[i] = base + i;
    d.seal_inputs();
    body(d, in);
    return d.value();
}

}  // namespace emp
#endif  // EMP_CONTEXT_DIGEST_H__
