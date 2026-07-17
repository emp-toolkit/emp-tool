#ifndef EMP_CONTEXT_RECORD_H__
#define EMP_CONTEXT_RECORD_H__

// RecordCtx — the recorder for templated BooleanContext kernels. Runs a kernel
// and emits a validated ir BooleanProgram (the IDENTICAL circuit replayed by
// every other context). Call external_input(n) BEFORE any gate so inputs are
// wires [0, num_inputs); then finish(output_wires). The emitted program is the
// same IR the shipped .empbc builtins use, so a kernel recorded through RecordCtx
// replays through any other context identically (ir/execute.h).

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/ir/program.h"
#include "emp-tool/ir/validate.h"
#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstdint>
#include <span>

namespace emp {

struct RecordCtx {
    using Wire = uint32_t;
    circuit::BooleanProgram prog;
    uint32_t next_id = 0, num_inputs = 0;
    int64_t c0 = -1, c1 = -1;   // dedup the two constant wires
    bool inputs_closed = false; // flips once any gate is emitted (see alloc_)

    uint32_t alloc_() { inputs_closed = true; expecting(next_id != UINT32_MAX, "RecordCtx: wire id overflow"); return next_id++; }
    Wire external_input(size_t n) {
        // Contract: all inputs are reserved before any gate, so inputs occupy
        // wires [0, num_inputs). A late external_input would interleave input ids
        // with gate ids and corrupt replay. Always enforced (cheap, record-path
        // only) so corruption can't slip through a release build.
        expecting(!inputs_closed,
                  "RecordCtx::external_input: called after a gate was emitted");
        expecting(n != 0, "RecordCtx::external_input: zero-width argument");
        expecting(n <= static_cast<size_t>(UINT32_MAX - next_id),
                  "RecordCtx::external_input: wire id overflow");
        expecting(n <= static_cast<size_t>(UINT32_MAX - num_inputs),
                  "RecordCtx::external_input: num_inputs overflow");
        Wire base = next_id; next_id += (uint32_t)n; num_inputs += (uint32_t)n; return base;
    }

    Wire public_bit(bool v) {
        int64_t& c = v ? c1 : c0;
        if (c < 0) { c = alloc_(); prog.gates.push_back({0, 0, (uint32_t)c,
                       v ? circuit::Op::Const1 : circuit::Op::Const0}); }
        return (Wire)c;
    }
    Wire and_gate(Wire a, Wire b) { Wire o = alloc_(); prog.gates.push_back({a, b, o, circuit::Op::And}); return o; }
    Wire xor_gate(Wire a, Wire b) { Wire o = alloc_(); prog.gates.push_back({a, b, o, circuit::Op::Xor}); return o; }
    Wire not_gate(Wire a)         { Wire o = alloc_(); prog.gates.push_back({a, 0, o, circuit::Op::Not}); return o; }

    // Set outputs, finalize dimensions, and validate the IR invariants.
    circuit::BooleanProgram& finish(std::span<const Wire> outputs) {
        prog.num_wires  = next_id;
        prog.num_inputs = num_inputs;
        prog.outputs.assign(outputs.begin(), outputs.end());
        circuit::validate_program(prog);
        return prog;
    }
};

static_assert(BooleanContext<RecordCtx>);

}  // namespace emp
#endif  // EMP_CONTEXT_RECORD_H__
