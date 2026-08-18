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
#include "emp-tool/ir/validate.h"
#include "emp-tool/ir/passes.h"        // schedule_pass
#include "emp-tool/ir/execute.h"       // ProgramWorkspace
#include "emp-tool/ir/context/concept.h"
#include "emp-tool/runtime/core/utils.h"       // error()
#include <span>
#include <utility>
#include <vector>

namespace emp {

namespace detail {

struct ScheduledPlan {
    std::vector<std::vector<uint32_t>> bucket;   // gate indices per level (emission order)
    int depth = 0;
};

inline ScheduledPlan make_scheduled_plan(const circuit::BooleanProgram& p) {
    circuit::ScheduleStats sched = circuit::schedule_pass(p);
    ScheduledPlan plan;
    plan.depth = sched.levels.depth;
    plan.bucket.assign((size_t)plan.depth + 1, {});
    for (uint32_t gi = 0; gi < p.gates.size(); ++gi)
        plan.bucket[sched.wire_level[p.gates[gi].out]].push_back(gi);
    return plan;
}

template <BulkBooleanContext Ctx>
inline const std::vector<typename Ctx::Wire>& execute_scheduled(
    Ctx& ctx, const circuit::BooleanProgram& p, const ScheduledPlan& plan,
    std::span<const typename Ctx::Wire> inputs,
    ProgramWorkspace<typename Ctx::Wire>& ws) {
    using W = typename Ctx::Wire;
    expecting(inputs.size() == p.num_inputs,
              "scheduled_execute_program: input count != program num_inputs");

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

}  // namespace detail

// Owns a validated dense program and its precomputed AND-depth schedule.
class ScheduledProgram {
public:
    explicit ScheduledProgram(circuit::BooleanProgram program)
        : program_(std::move(program)) {
        circuit::validate_program(program_);
        circuit::require_dense(program_, "ScheduledProgram");
        plan_ = detail::make_scheduled_plan(program_);
    }

    const circuit::BooleanProgram& program() const { return program_; }

private:
    circuit::BooleanProgram program_;
    detail::ScheduledPlan plan_;

    template <BulkBooleanContext Ctx>
    friend const std::vector<typename Ctx::Wire>& scheduled_execute_program(
        Ctx&, const ScheduledProgram&,
        std::span<const typename Ctx::Wire>,
        ProgramWorkspace<typename Ctx::Wire>&);
};

// Replay a prepared program using a reusable workspace.
template <BulkBooleanContext Ctx>
inline const std::vector<typename Ctx::Wire>& scheduled_execute_program(
    Ctx& ctx, const ScheduledProgram& scheduled,
    std::span<const typename Ctx::Wire> inputs,
    ProgramWorkspace<typename Ctx::Wire>& ws) {
    return detail::execute_scheduled(ctx, scheduled.program_, scheduled.plan_,
                                     inputs, ws);
}

// Convenience: precomputed plan, allocate outputs.
template <BulkBooleanContext Ctx>
inline std::vector<typename Ctx::Wire> scheduled_execute_program(
    Ctx& ctx, const ScheduledProgram& scheduled,
    std::span<const typename Ctx::Wire> inputs) {
    ProgramWorkspace<typename Ctx::Wire> ws;
    scheduled_execute_program(ctx, scheduled, inputs, ws);
    return std::move(ws.out);
}

// One-shot convenience. Repeated callers should move the program into a
// ScheduledProgram so validation and scheduling happen only once.
template <BulkBooleanContext Ctx>
inline std::vector<typename Ctx::Wire> scheduled_execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p,
    std::span<const typename Ctx::Wire> inputs) {
    circuit::validate_program(p);
    circuit::require_dense(p, "scheduled_execute_program");
    detail::ScheduledPlan plan = detail::make_scheduled_plan(p);
    ProgramWorkspace<typename Ctx::Wire> ws;
    detail::execute_scheduled(ctx, p, plan, inputs, ws);
    return std::move(ws.out);
}

}  // namespace emp
#endif  // EMP_IR_SCHEDULE_H__
