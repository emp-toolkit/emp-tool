// Cross-context portability of the BooleanContext circuit frontend: compile a
// circuit ONCE (through a RecordCtx) into a context-free Circuit, then run
// the SAME Circuit on Ctx. Exercises both body forms, a nullary
// circuit, the .constant() sugar, and an fp32 IR-replay body. The 32-bit adder
// is the size-optimal 31-AND kernel; recording is deterministic.
// run(ctx, circuit, ws, args...) with one caller workspace across calls and
// circuits matches the one-shot run and the execute_program layer below it on
// ClearCtx, RecordCtx, a half-gate garbler and the refcounted chunk recorder,
// and records the same opaque unit calls on a ComposeCtx. C++20.

#include "emp-tool/emp-tool.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/ir/session/chunk_recorder.h"
#include "emp-tool/ir/context/context.h"
#include "emp-tool/circuits/typed.h"
#include "emp-tool/circuits/frontend/circuit_fn.h"
#include "emp-tool/circuits/frontend/rec.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <span>
#include <stdexcept>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

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

// ---- run(ctx, circuit, ws, args...): one workspace across calls and circuits ----

// How a sweep replays each circuit: the one-shot run, run with the caller's
// workspace, or execute_program over hand-packed inputs (the layer below the
// frontend, sharing none of run's input or workspace handling).
enum class Form { OneShot, Workspace, Lower };

// Appends a wire to a log: its bytes, or a recorder wire's id.
template <class Wire>
static void log_wire(std::vector<uint8_t>& log, const Wire& w) {
    if constexpr (std::is_trivially_copyable_v<Wire>) {
        const auto* p = reinterpret_cast<const uint8_t*>(&w);
        log.insert(log.end(), p, p + sizeof(Wire));
    } else {
        log_wire(log, (uint32_t)w);
    }
}

static void log_gates(std::vector<uint8_t>& log, const std::vector<circuit::Gate>& gates) {
    for (const circuit::Gate& g : gates) {
        log_wire(log, g.in0); log_wire(log, g.in1); log_wire(log, g.out);
        log.push_back((uint8_t)g.op);
    }
}

// The circuits a sweep alternates between: dense programs of 0 to 256 inputs and
// 2 to 39312 wires (the swap has no gates at all), and the Linear and Full
// wire-reuse forms of a multiplier, which write some slots more than once.
struct SweepCircuits {
    cf::Circuit<rec::BitVec<128>, rec::BitVec<128>, rec::BitVec<128>> aes;
    cf::Circuit<rec::UInt<32>, rec::UInt<32>, rec::UInt<32>> add32, mul32, mul32_linear, mul32_full;
    cf::Circuit<rec::UInt<32>, rec::UInt<32>> add7;
    cf::Circuit<rec::UInt<16>> konst;
    cf::Circuit<rec::BitVec<8>, rec::BitVec<8>> swap;
    cf::Circuit<rec::Float<32>, rec::Float<32>, rec::Float<32>> fadd;
    cf::Circuit<rec::UInt<128>, rec::UInt<128>, rec::UInt<128>> add128;
};

static SweepCircuits sweep_circuits() {
    auto add = [](auto a, auto b) { return a + b; };
    auto mul = [](auto a, auto b) { return a * b; };
    auto mul32 = cf::compile<rec::UInt<32>, rec::UInt<32>>(mul);
    circuit::CircuitArtifact full{
        circuit::make_compact(mul32.program(), circuit::WireReuse::Full).prog, mul32.signature()};
    circuit::CircuitArtifact aes{circuit::builtin_circuit("aes128"),
                                 circuit::CircuitSignature{{128, 128}, 128}};
    return SweepCircuits{
        decltype(SweepCircuits::aes)(aes),
        cf::compile<rec::UInt<32>, rec::UInt<32>>(add),
        mul32,
        cf::compile_linear<rec::UInt<32>, rec::UInt<32>>(mul),
        decltype(SweepCircuits::mul32_full)(full),
        cf::compile<rec::UInt<32>>([](auto a) { return a + a.constant(7); }),
        cf::compile<>([](auto& ctx) {
            using C = std::remove_reference_t<decltype(ctx)>;
            return UInt_T<C, 16>::constant(ctx, 4242);
        }),
        cf::compile<rec::BitVec<8>>([](auto b) {
            return b.template slice<4, 8>().concat(b.template slice<0, 4>());
        }),
        cf::compile<rec::Float<32>, rec::Float<32>>(add),
        cf::compile<rec::UInt<128>, rec::UInt<128>>(add),
    };
}

// Runs every circuit of `s` on ctx in one fixed order, three times over, so the
// workspace grows and shrinks from call to call. Argument wires come from
// arg_wires(n). The log gets each call's result, then whatever after_call adds
// about the context. In the Workspace form every slot the workspace holds is set
// to *junk before each call (when junk is given), so a replay that read a slot it
// had not written in that call would change the log.
template <class C, class ArgWires, class AfterCall>
static std::vector<uint8_t> sweep(const SweepCircuits& s, C& ctx, Form form,
                                  ProgramWorkspace<typename C::Wire>& ws,
                                  const typename C::Wire* junk, ArgWires&& arg_wires,
                                  AfterCall&& after_call) {
    using Wire = typename C::Wire;
    std::vector<uint8_t> log;
    auto call = [&](const auto& circ, const auto&... args) {
        std::vector<Wire> out;
        if (form == Form::Lower) {
            std::vector<Wire> in;
            auto pack = [&](const auto& v) {
                const std::size_t at = in.size();
                in.resize(at + (std::size_t)v.width());
                v.pack_wires(in.data() + at);
            };
            (pack(args), ...);
            out = execute_program(ctx, circ.program(), std::span<const Wire>(in.data(), in.size()));
        } else {
            if (form == Form::Workspace && junk != nullptr)
                for (std::vector<Wire>* v : {&ws.scratch.wires, &ws.out, &ws.tmp_inputs})
                    std::fill(v->begin(), v->end(), *junk);
            auto r = form == Form::Workspace ? cf::run(ctx, circ, ws, args...)
                                             : cf::run(ctx, circ, args...);
            out.resize((std::size_t)r.width());
            r.pack_wires(out.data());
        }
        for (const Wire& w : out) log_wire(log, w);
        out.clear();   // drop held wires before after_call looks at the context
        after_call(log);
    };
    auto arg = [&](auto type) {
        using V = typename decltype(type)::type;
        std::vector<Wire> w = arg_wires(V::width());
        return V::from_wires(ctx, w.data());
    };
    using U32 = std::type_identity<UInt_T<C, 32>>;
    using U128 = std::type_identity<UInt_T<C, 128>>;
    using F32 = std::type_identity<Float_T<C, 32>>;
    using BV8 = std::type_identity<BitVec_T<C, 8>>;
    using BV128 = std::type_identity<BitVec_T<C, 128>>;
    for (int pass = 0; pass < 3; ++pass) {
        { auto k = arg(BV128{}), x = arg(BV128{}); call(s.aes, k, x); }
        { auto a = arg(U32{}), b = arg(U32{}); call(s.add32, a, b); }
        call(s.konst);
        { auto a = arg(U32{}), b = arg(U32{}); call(s.mul32_full, a, b); }
        { auto a = arg(U32{}); call(s.add7, a); }
        { auto a = arg(BV8{}); call(s.swap, a); }
        { auto a = arg(U32{}), b = arg(U32{}); call(s.mul32_linear, a, b); }
        { auto a = arg(F32{}), b = arg(F32{}); call(s.fadd, a, b); }
        { auto a = arg(U128{}), b = arg(U128{}); call(s.add128, a, b); }
        { auto a = arg(U32{}), b = arg(U32{}); call(s.mul32, a, b); }
    }
    return log;
}

// A half-gate garbler, the shape of a garbling backend's context: Wire = label,
// and every AND appends its two table rows.
struct HalfGateGarbler {
    using Wire = block;
    block delta = zero_block, zero = zero_block;   // zero: the label of public false
    MITCCRH<> hash;
    std::vector<block> table;
    Wire public_bit(bool v) { return v ? zero ^ delta : zero; }
    Wire xor_gate(Wire a, Wire b) { return a ^ b; }
    Wire not_gate(Wire a) { return a ^ delta; }
    Wire and_gate(Wire a, Wire b) {
        block rows[2];
        const block c = halfgates_garble(a, a ^ delta, b, b ^ delta, delta, rows, &hash);
        table.insert(table.end(), rows, rows + 2);
        return c;
    }
};
static_assert(BooleanContext<HalfGateGarbler>);

static void sweep_workspace_reuse() {
    const SweepCircuits s = sweep_circuits();
    chk("sweep set: Linear and Full reuse forms",
        s.mul32_linear.program().wire_reuse == circuit::WireReuse::Linear &&
        s.mul32_full.program().wire_reuse == circuit::WireReuse::Full &&
        s.mul32_full.program().num_wires < s.mul32_linear.program().num_wires);
    chk("sweep set: a program with no gates", s.swap.program().gates.empty());
    auto nothing = [](std::vector<uint8_t>&) {};

    // ClearCtx: stateless, so all three forms share one context.
    {
        ClearCtx cc;
        ProgramWorkspace<uint8_t> ws, unused;
        const uint8_t junk = 0xA5;
        std::mt19937 rng;
        auto bits = [&](int n) {
            std::vector<uint8_t> w((std::size_t)n);
            for (auto& b : w) b = (uint8_t)(rng() & 1);
            return w;
        };
        rng.seed(1); auto lower = sweep(s, cc, Form::Lower, unused, nullptr, bits, nothing);
        rng.seed(1); auto once = sweep(s, cc, Form::OneShot, unused, nullptr, bits, nothing);
        rng.seed(1); auto reused = sweep(s, cc, Form::Workspace, ws, &junk, bits, nothing);
        chk("workspace sweep, ClearCtx: one-shot run == execute_program", once == lower);
        chk("workspace sweep, ClearCtx: reused workspace == one-shot run", reused == once);
    }

    // RecordCtx: each form records into its own context; the recorded gate
    // streams must match too.
    {
        ProgramWorkspace<uint32_t> ws, unused;
        const uint32_t junk = 0xFFFFFF00u;   // never a wire id of these recordings
        auto record = [&](Form form) {
            RecordCtx rc;
            const uint32_t base = rc.external_input(1024);
            std::mt19937 rng(2);
            auto ids = [&](int n) {
                const uint32_t at = base + (uint32_t)(rng() % (uint32_t)(1024 - n + 1));
                std::vector<uint32_t> w((std::size_t)n);
                for (int i = 0; i < n; ++i) w[(std::size_t)i] = at + (uint32_t)i;
                return w;
            };
            auto log = sweep(s, rc, form, form == Form::Workspace ? ws : unused, &junk, ids, nothing);
            log_gates(log, rc.prog.gates);
            return log;
        };
        const auto lower = record(Form::Lower), once = record(Form::OneShot);
        const auto reused = record(Form::Workspace);
        chk("workspace sweep, RecordCtx: one-shot run == execute_program", once == lower);
        chk("workspace sweep, RecordCtx: reused workspace == one-shot run", reused == once);
    }

    // A half-gate garbler: labels and garbled tables must match byte for byte.
    {
        ProgramWorkspace<block> ws, unused;
        const block junk = makeBlock(0xA5A5A5A5A5A5A5A5ull, 0x5A5A5A5A5A5A5A5Aull);
        auto garble = [&](Form form) {
            const block seed = makeBlock(0x0123456789abcdefull, 0xfedcba9876543210ull);
            PRG prg(&seed);
            HalfGateGarbler g;
            prg.random_block(&g.delta, 1);
            g.delta = g.delta | makeBlock(0, 1);
            prg.random_block(&g.zero, 1);
            block hash_seed;
            prg.random_block(&hash_seed, 1);
            g.hash.setS(hash_seed);
            auto labels = [&](int n) {
                std::vector<block> w((std::size_t)n);
                prg.random_block(w.data(), n);
                return w;
            };
            auto log = sweep(s, g, form, form == Form::Workspace ? ws : unused, &junk, labels, nothing);
            for (const block& t : g.table) log_wire(log, t);
            return log;
        };
        const auto lower = garble(Form::Lower), once = garble(Form::OneShot);
        const auto reused = garble(Form::Workspace);
        chk("workspace sweep, garbler: one-shot run == execute_program", once == lower);
        chk("workspace sweep, garbler: reused workspace == one-shot run", reused == once);
    }

    // The chunk recorder's refcounted wires: after every call the refcount table
    // must match the one-shot form's, so the workspace holds no wire between
    // calls. The workspace outlives its recorder, whose teardown stops the
    // process if any wire is still held, so the reused-workspace log is compared
    // while its recorder is alive. One recorder is active at a time.
    {
        using CW = ChunkRecorderCtx::Wire;
        ProgramWorkspace<CW> ws, unused;
        auto record = [&](Form form, const std::vector<uint8_t>* expect) {
            ChunkRecorderCtx rec;
            const std::vector<uint32_t> pool = rec.reserve_ids(1024);
            std::mt19937 rng(3);
            auto ids = [&](int n) {
                const std::size_t at = rng() % (std::size_t)(1024 - n + 1);
                return std::vector<CW>(pool.begin() + (std::ptrdiff_t)at,
                                       pool.begin() + (std::ptrdiff_t)(at + (std::size_t)n));
            };
            auto refcounts = [&](std::vector<uint8_t>& log) {
                for (int c : rec.rc_) log_wire(log, c);
            };
            auto log = sweep(s, rec, form, form == Form::Workspace ? ws : unused, nullptr, ids, refcounts);
            log_gates(log, rec.chunk_gates());
            if (expect != nullptr) {
                chk("workspace sweep, chunk recorder: reused workspace == one-shot run",
                    log == *expect);
                std::fflush(stdout);   // the teardown check below exits without flushing
            }
            return log;
        };
        const auto lower = record(Form::Lower, nullptr);
        const auto once = record(Form::OneShot, nullptr);
        chk("workspace sweep, chunk recorder: one-shot run == execute_program", once == lower);
        (void)record(Form::Workspace, &once);
        chk("workspace sweep, chunk recorder: the workspace keeps no wire",
            ws.scratch.wires.empty() && ws.out.empty() && ws.tmp_inputs.empty());
    }

    // ComposeCtx records each run as one opaque unit call; the workspace form
    // records the same plan.
    {
        auto plan_of = [&](bool with_ws) {
            ComposeCtx cc;
            ProgramWorkspace<uint32_t> w;
            const uint32_t base = cc.external_input(64);
            std::array<uint32_t, 64> in{};
            for (uint32_t i = 0; i < 64; ++i) in[i] = base + i;
            auto x = UInt_T<ComposeCtx, 32>::from_wires(cc, in.data());
            auto y = UInt_T<ComposeCtx, 32>::from_wires(cc, in.data() + 32);
            auto z = with_ws ? cf::run(cc, s.add32, w, x, y) : cf::run(cc, s.add32, x, y);
            auto k = with_ws ? cf::run(cc, s.konst, w) : cf::run(cc, s.konst);
            auto r = with_ws ? cf::run(cc, s.add7, w, z) : cf::run(cc, s.add7, z);
            std::array<uint32_t, 48> out{};
            r.pack_wires(out.data());
            k.pack_wires(out.data() + 32);
            ComposePlan plan = cc.finish(std::span<const uint32_t>(out.data(), out.size()));
            std::vector<uint8_t> log;
            for (const ComposeInstance& inst : plan.instances) {
                log_wire(log, inst.unit.get());
                for (uint32_t id : inst.in_ids) log_wire(log, id);
                for (uint32_t id : inst.out_ids) log_wire(log, id);
            }
            for (uint32_t id : plan.outputs) log_wire(log, id);
            log_wire(log, plan.num_wires);
            log_wire(log, plan.events.size());
            return log;
        };
        chk("ComposeCtx: the workspace form records the same unit calls",
            plan_of(true) == plan_of(false));
    }
}

// A context whose Wire holds a resource (a shared_ptr) and whose AND gate throws
// once `limit` ANDs have run. When a replay through a caller's workspace unwinds,
// the workspace must hold no wire afterwards, as after a normal return.
struct ThrowingCtx {
    using Wire = std::shared_ptr<const int>;
    int ands = 0, limit = 0;
    Wire public_bit(bool v) { return std::make_shared<const int>(v ? 1 : 0); }
    Wire xor_gate(const Wire& a, const Wire& b) { return std::make_shared<const int>(*a ^ *b); }
    Wire not_gate(const Wire& a) { return std::make_shared<const int>(*a ^ 1); }
    Wire and_gate(const Wire& a, const Wire& b) {
        if (++ands > limit) throw std::runtime_error("ThrowingCtx: AND limit reached");
        return std::make_shared<const int>(*a & *b);
    }
};
static_assert(BooleanContext<ThrowingCtx>);

static void sweep_workspace_unwind() {
    const SweepCircuits s = sweep_circuits();
    ThrowingCtx ctx;
    std::array<ThrowingCtx::Wire, 32> xw, yw;
    for (int i = 0; i < 32; ++i) {
        xw[i] = ctx.public_bit((0x9e3779b9u >> i) & 1);
        yw[i] = ctx.public_bit((0x7f4a7c15u >> i) & 1);
    }
    auto x = UInt_T<ThrowingCtx, 32>::from_wires(ctx, xw.data());
    auto y = UInt_T<ThrowingCtx, 32>::from_wires(ctx, yw.data());
    ProgramWorkspace<ThrowingCtx::Wire> ws;

    // A full run first, so the workspace has grown, then runs cut short at
    // several points of the replay.
    ctx.limit = 1 << 30;
    (void)cf::run(ctx, s.mul32, ws, x, y);
    chk("workspace unwind: a returning call keeps no wire",
        ws.scratch.wires.empty() && ws.out.empty() && ws.tmp_inputs.empty());
    for (int limit : {0, 1, 100, 900}) {
        ctx.ands = 0;
        ctx.limit = limit;
        bool threw = false;
        try {
            (void)cf::run(ctx, s.mul32, ws, x, y);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        chk("workspace unwind: the context threw", threw);
        chk("workspace unwind: an unwinding call keeps no wire",
            ws.scratch.wires.empty() && ws.out.empty() && ws.tmp_inputs.empty());
    }
    // Every input wire is held only by the caller's arrays and values again.
    bool held = true;
    for (int i = 0; i < 32; ++i)
        held = held && xw[i].use_count() == 2 && yw[i].use_count() == 2;
    chk("workspace unwind: no extra reference to an input wire survives", held);
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

    // 9) run(ctx, circuit, ws, args...): one caller workspace across calls and circuits.
    sweep_workspace_reuse();
    sweep_workspace_unwind();

    printf("test_circuit_fn: %s\n", bad ? "FAILED" : "compile-once / run-anywhere — PASS");
    return bad ? 1 : 0;
}
