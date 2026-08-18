#ifndef EMP_IR_SIMD_H__
#define EMP_IR_SIMD_H__

// SIMD-batched circuit replay: evaluate one BooleanProgram over N INDEPENDENT
// instances at once. 128 instances are packed per `block` lane and driven through
// the AND-layer-scheduled executor (ir/schedule.h) on a BulkBooleanContext whose
// Wire is a 128-lane block (e.g. a GMW backend, or a bitsliced clear context).
//
// I/O is flat and INSTANCE-MAJOR:
//   input  bit for instance t, program input  i  ==  in[t  * p.num_inputs    + i]
//   output bit for instance t, program output o  ==  out[t * p.outputs.size() + o]
//
// Because each Wire carries one bit per instance across its 128 lanes, a single
// and_many(out, a, b, n) covers n*128 instance-ANDs, and one protocol round (the
// AND layer's open) amortizes over the whole batch.

#include "emp-tool/ir/schedule.h"            // ScheduledProgram, scheduled_execute_program
#include "emp-tool/ir/execute.h"             // ProgramWorkspace
#include "emp-tool/ir/program.h"
#include "emp-tool/runtime/core/block.h"     // block
#include "emp-tool/runtime/core/utils.h"     // bool_to_block, bits_to_bools
#include <algorithm>
#include <span>
#include <type_traits>
#include <vector>

namespace emp {

// Prepared-program + workspace form: validation, scheduling, and per-level
// buffer allocation are not repeated across calls.
template <BulkBooleanContext Ctx>
inline void simd_execute_program(Ctx& ctx, const ScheduledProgram& scheduled,
                                 const bool* in, bool* out, int64_t N,
                                 ProgramWorkspace<typename Ctx::Wire>& ws) {
    static_assert(std::is_same_v<typename Ctx::Wire, block>,
                  "simd_execute_program: Ctx::Wire must be `block` (128 packed instance-lanes)");
    expecting(N >= 0, "simd_execute_program: instance count must be non-negative");
    using W = block;
    const circuit::BooleanProgram& p = scheduled.program();
    const uint32_t ni = p.num_inputs;
    const uint32_t no = (uint32_t)p.outputs.size();

    std::vector<W> inbuf(ni);
    bool bits[128];

    for (int64_t base = 0; base < N; base += 128) {
        const int64_t lanes = std::min<int64_t>(128, N - base);
        // Pack each program input across `lanes` instances into one 128-lane block
        // (lane t = instance base+t); pad the tail lanes when N is not a multiple of 128.
        for (uint32_t i = 0; i < ni; ++i) {
            for (int64_t t = 0; t < 128; ++t)
                bits[t] = (t < lanes) && in[(base + t) * ni + i];
            inbuf[i] = bool_to_block(bits);
        }
        const std::vector<W>& outw = scheduled_execute_program(
            ctx, scheduled, std::span<const W>(inbuf.data(), inbuf.size()), ws);
        // Unpack each output block back into the `lanes` live instances.
        for (uint32_t o = 0; o < no; ++o) {
            bits_to_bools(bits, &outw[o], 128);
            for (int64_t t = 0; t < lanes; ++t)
                out[(base + t) * no + o] = bits[t];
        }
    }
}

// Convenience: allocate only the reusable workspace. The program remains
// prepared and schedule-bound across calls.
template <BulkBooleanContext Ctx>
inline void simd_execute_program(Ctx& ctx, const ScheduledProgram& scheduled,
                                 const bool* in, bool* out, int64_t N) {
    ProgramWorkspace<typename Ctx::Wire> ws;
    simd_execute_program(ctx, scheduled, in, out, N, ws);
}

}  // namespace emp
#endif  // EMP_IR_SIMD_H__
