// ir/simd.h — SIMD-batched replay: run one BooleanProgram over N independent
// instances, 128 packed per block lane, via the AND-layer scheduler. Checked
// against a host formula over a bitsliced *clear* BulkBooleanContext (Wire=block,
// each lane an independent plaintext bit), including a non-128-multiple tail.
#include "emp-tool/ir/simd.h"
#include "emp-tool/ir/program.h"
#include "emp-tool/ir/context/concept.h"
#include "emp-tool/runtime/core/block.h"
#include "emp-tool/runtime/crypto/prg.h"
#include <cstdio>
#include <memory>
using namespace emp;
using namespace emp::circuit;

// A bitsliced plaintext backend: Wire is a block whose 128 lanes are 128
// independent clear bits. Models BulkBooleanContext, so the scheduler + the
// SIMD driver drive it exactly as they would a real GMW backend.
struct BitslicedClearCtx {
    using Wire = block;
    Wire public_bit(bool v)       { return v ? all_one_block : zero_block; }
    Wire and_gate(Wire a, Wire b) { return a & b; }
    Wire xor_gate(Wire a, Wire b) { return a ^ b; }
    Wire not_gate(Wire a)         { return a ^ all_one_block; }
    void and_many(Wire* o, const Wire* a, const Wire* b, size_t n) {
        for (size_t i = 0; i < n; ++i) o[i] = a[i] & b[i];
    }
};
static_assert(BulkBooleanContext<BitslicedClearCtx>);

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }

// out = ((a & b) & (c & d)) ^ a  — two AND-depth levels (the first batches two
// ANDs into one and_many call) plus a linear XOR.
static BooleanProgram make_prog() {
    BooleanProgram p;
    p.num_inputs = 4;
    p.num_wires  = 8;
    p.gates = {
        {0, 1, 4, Op::And},   // t1 = a & b
        {2, 3, 5, Op::And},   // t2 = c & d
        {4, 5, 6, Op::And},   // t3 = t1 & t2
        {6, 0, 7, Op::Xor},   // out = t3 ^ a
    };
    p.outputs = {7};
    return p;
}

int main() {
    BitslicedClearCtx ctx;
    BooleanProgram p = make_prog();
    PRG prg;

    for (int64_t N : {int64_t(1), int64_t(64), int64_t(128), int64_t(200), int64_t(257)}) {
        auto in  = std::make_unique<bool[]>((size_t)N * 4);
        auto out = std::make_unique<bool[]>((size_t)N);
        prg.random_bool(in.get(), (int)(N * 4));

        simd_execute_program(ctx, p, in.get(), out.get(), N);

        bool ok = true;
        for (int64_t t = 0; t < N; ++t) {
            const bool a = in[t * 4 + 0], b = in[t * 4 + 1],
                       c = in[t * 4 + 2], d = in[t * 4 + 3];
            const bool want = ((a & b) & (c & d)) ^ a;
            if (out[t] != want) { ok = false; printf("    N=%lld inst=%lld got=%d want=%d\n",
                                                     (long long)N, (long long)t, out[t], want); }
        }
        char name[32]; snprintf(name, sizeof(name), "simd N=%lld", (long long)N);
        chk(name, ok);
    }

    printf("test_simd: %s\n", bad ? "FAILED" : "PASS");
    return bad ? 1 : 0;
}
