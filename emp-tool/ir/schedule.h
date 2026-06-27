#ifndef EMP_IR_SCHEDULE_H__
#define EMP_IR_SCHEDULE_H__

// Scheduled bulk replay: execute linear (const/xor/not) gates in topological
// order, but batch every *ready* AND layer (grouped by AND-depth via
// schedule_pass) into one and_many call on a BulkBooleanContext. Correctness is
// identical to the scalar execute_program (ir/execute.h); the layering is what
// lets a bulk backend amortize crypto. Per AND-depth level L: run the level's
// ANDs first (operands are all at depth < L, hence ready), then the level's
// linear gates in emission order (they may read this level's ANDs or earlier
// same-level linears).

#include "emp-tool/ir/program.h"
#include "emp-tool/ir/passes.h"        // schedule_pass
#include "emp-tool/ir/execute.h"       // ProgramWorkspace
#include "emp-tool/ir/context/concept.h"
#include "emp-tool/ir/context/digest.h"   // digest_program (plan staleness guard)
#include "emp-tool/runtime/core/utils.h"       // error()
#include <span>
#include <vector>

namespace emp {

// A precomputed schedule: gate indices grouped per AND-depth level. Build once
// with make_scheduled_plan() and reuse across many executions of the same program
// (e.g. an AG2PC/AG-MPC builtin replayed every call) so the schedule pass and
// bucket allocation are not repeated per run.
struct ScheduledPlan {
    std::vector<std::vector<uint32_t>> bucket;   // gate indices per level (emission order)
    int depth = 0;
    // Identity of the program this plan was built for. A plan reused against a
    // DIFFERENT program would silently skip/misorder gates, so execution checks
    // these: the dimensions are an O(1) always-on guard; the digest is a stronger
    // debug-only guard (same shape, different gate stream).
    uint32_t num_inputs = 0;
    uint32_t num_wires  = 0;
    size_t   num_gates  = 0;
    uint64_t digest     = 0;
};

inline ScheduledPlan make_scheduled_plan(const circuit::BooleanProgram& p) {
    circuit::ScheduleStats sched = circuit::schedule_pass(p);
    ScheduledPlan plan;
    plan.depth = sched.levels.depth;
    plan.bucket.assign((size_t)plan.depth + 1, {});
    for (uint32_t gi = 0; gi < p.gates.size(); ++gi)
        plan.bucket[sched.wire_level[p.gates[gi].out]].push_back(gi);
    plan.num_inputs = p.num_inputs;
    plan.num_wires  = p.num_wires;
    plan.num_gates  = p.gates.size();
    plan.digest     = digest_program(p);
    return plan;
}

// Replay using a precomputed plan + reusable workspace (no schedule pass, no
// allocation per call). Writes outputs into ws.out and returns a reference.
template <BulkBooleanContext Ctx>
inline const std::vector<typename Ctx::Wire>& scheduled_execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p, const ScheduledPlan& plan,
    std::span<const typename Ctx::Wire> inputs,
    ProgramWorkspace<typename Ctx::Wire>& ws) {
    using W = typename Ctx::Wire;
    circuit::require_dense(p, "scheduled_execute_program");
    if (inputs.size() != p.num_inputs)
        error("scheduled_execute_program: input count != program num_inputs");
    if (plan.num_inputs != p.num_inputs || plan.num_wires != p.num_wires ||
        plan.num_gates != p.gates.size())
        error("scheduled_execute_program: plan does not match program (stale plan?)");
#ifndef NDEBUG
    if (plan.digest != digest_program(p))
        error("scheduled_execute_program: plan digest mismatch (stale plan for a different program)");
#endif

    ws.scratch.ensure(p.num_wires);
    W* w = ws.scratch.wires.data();
    for (uint32_t i = 0; i < p.num_inputs; ++i) w[i] = inputs[i];

    for (int lv = 0; lv <= plan.depth; ++lv) {
        const std::vector<uint32_t>& bucket = plan.bucket[lv];
        ws.ba.clear(); ws.bb.clear(); ws.bouts.clear();
        for (uint32_t gi : bucket) {
            const circuit::Gate& g = p.gates[gi];
            if (g.op == circuit::Op::And) { ws.ba.push_back(w[g.in0]); ws.bb.push_back(w[g.in1]); ws.bouts.push_back(g.out); }
        }
        if (!ws.ba.empty()) {
            ws.bo.assign(ws.ba.size(), W{});
            ctx.and_many(ws.bo.data(), ws.ba.data(), ws.bb.data(), ws.ba.size());
            for (size_t k = 0; k < ws.bouts.size(); ++k) w[ws.bouts[k]] = ws.bo[k];
        }
        for (uint32_t gi : bucket) {
            const circuit::Gate& g = p.gates[gi];
            switch (g.op) {
                case circuit::Op::And:    break;                                  // done above
                case circuit::Op::Xor:    w[g.out] = ctx.xor_gate(w[g.in0], w[g.in1]); break;
                case circuit::Op::Not:    w[g.out] = ctx.not_gate(w[g.in0]); break;
                case circuit::Op::Const0: w[g.out] = ctx.public_bit(false); break;
                case circuit::Op::Const1: w[g.out] = ctx.public_bit(true); break;
            }
        }
    }
    ws.out.resize(p.outputs.size());
    for (size_t i = 0; i < p.outputs.size(); ++i) ws.out[i] = w[p.outputs[i]];
    return ws.out;
}

// Convenience: precomputed plan, allocate outputs.
template <BulkBooleanContext Ctx>
inline std::vector<typename Ctx::Wire> scheduled_execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p, const ScheduledPlan& plan,
    std::span<const typename Ctx::Wire> inputs) {
    ProgramWorkspace<typename Ctx::Wire> ws;
    return scheduled_execute_program(ctx, p, plan, inputs, ws);
}

// Convenience: build a one-shot plan and replay. For repeated execution of the
// same program, cache make_scheduled_plan(p) and use the plan overloads above.
template <BulkBooleanContext Ctx>
inline std::vector<typename Ctx::Wire> scheduled_execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p,
    std::span<const typename Ctx::Wire> inputs) {
    return scheduled_execute_program(ctx, p, make_scheduled_plan(p), inputs);
}

}  // namespace emp
#endif  // EMP_IR_SCHEDULE_H__
