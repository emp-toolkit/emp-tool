#ifndef EMP_FRONTEND_CIRCUIT_FN_H__
#define EMP_FRONTEND_CIRCUIT_FN_H__

// The circuit-function frontend over the C++20 BooleanContext model: write a
// PURE circuit body once, compile() it into a Circuit<RetV, ArgVs...> (a recorded
// BooleanProgram + signature, named by value types over RecordCtx), and run() it
// on ANY context — plaintext, garbled 2PC, ZK, ... — exactly like the built-in
// .empbc circuits. No global backend; the context is passed explicitly. Values are
// Bit_T<Ctx> / UInt_T<Ctx,N> / Int_T<Ctx,N> / Float_T<Ctx,W>; compile names them
// via the rec:: aliases, e.g. rec::UInt<32> == UInt_T<RecordCtx,32>.
//
// A circuit is PURE: no input/reveal/OT inside a body (RecordCtx has no I/O,
// so it is structurally impossible). I/O is a SESSION concern, AROUND run():
//   auto a = sess.input<UInt_T<Ctx,32>>(ALICE, av);    // feed inputs through the session
//   auto c = frontend::run(sess.ctx(), circuit, a, b); // run the circuit over the context
//   auto out = sess.reveal(c, PUBLIC);                 // results leave through the session
//
// A body comes in two forms; compile/run pick whichever is invocable:
//   [](auto a, auto b){ return a + b; }          // implicit context (default)
//   [](auto& ctx, auto a, auto b){ ... }         // explicit context (general:
//                                                //  the only form for nullary
//                                                //  circuits and for making a
//                                                //  constant with no anchor arg)
// A body callable in BOTH forms (e.g. a variadic lambda) is a contract error.
// In the implicit form, make a constant from an argument: a.constant(5). C++20.

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/ir/context/record.h"      // RecordCtx
#include "emp-tool/ir/context/compose.h"     // ComposeCtx (run() records opaque instances on it)
#include "emp-tool/ir/execute.h"          // execute_program, ProgramWorkspace
#include "emp-tool/ir/transform.h"        // make_compact, WireReuse
#include "emp-tool/ir/artifact.h" // CircuitArtifact, CircuitSignature, validate_artifact
#include "emp-tool/ir/wire_value.h"             // WireBundle (the structural value concept this frontend speaks)
#include "emp-tool/circuits/value_traits.h"     // value_traits (metadata accessor)
#include "emp-tool/circuits/typed.h"            // typed values (Bit_T/UInt_T/Int_T/Float_T)
#include "emp-tool/runtime/core/utils.h"                // error()
#include <array>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace emp {
namespace frontend {

// The value concept this frontend speaks is emp::WireBundle (ir/wire_value.h)
// — execution only packs/unpacks wires, so a clear codec is NOT required of a
// circuit argument or return (sessions, the codec consumers, constrain on
// WireValue). A circuit value whose context is the recording context is a
// RecordValue — the only valid argument type to compile() (its body is recorded
// through a RecordCtx). Use the rec:: aliases (frontend/rec.h): rec::UInt<32> ==
// UInt_T<RecordCtx,32>.
template <class V_>
concept RecordValue =
    WireBundle<V_> &&
    std::same_as<typename std::decay_t<V_>::context_type, RecordCtx>;

// ---------------------------------------------------------------------------
// circuit_fn_traits — introspect a body over typed args, in either form.
// ---------------------------------------------------------------------------
namespace detail {
template <class Ctx, class F, bool Impl, bool Expl, class... Args>
struct ret_ { using type = void; };
template <class Ctx, class F, class... Args>
struct ret_<Ctx, F, true, false, Args...> {
    using type = decltype(std::declval<F&>()(std::declval<Args>()...));
};
template <class Ctx, class F, class... Args>
struct ret_<Ctx, F, false, true, Args...> {
    using type = decltype(std::declval<F&>()(std::declval<Ctx&>(), std::declval<Args>()...));
};
}  // namespace detail

template <class Ctx, class F, class... Args>
struct circuit_fn_traits {
    static constexpr bool implicit_callable = std::is_invocable_v<F&, Args...>;
    static constexpr bool explicit_callable = std::is_invocable_v<F&, Ctx&, Args...>;
    static constexpr bool ambiguous = implicit_callable && explicit_callable;
    static constexpr bool callable  = implicit_callable || explicit_callable;
    static constexpr bool wants_ctx = explicit_callable && !implicit_callable;

    using raw_return = typename detail::ret_<Ctx, F, implicit_callable && !ambiguous,
                                             explicit_callable && !ambiguous, Args...>::type;
    using value_return = std::decay_t<raw_return>;

    static constexpr bool args_are_circuit = (true && ... && WireBundle<Args>);
    static constexpr bool returns_ref      = std::is_reference_v<raw_return>;
    static constexpr bool returns_void     = std::is_void_v<raw_return>;
    static constexpr bool returns_circuit  = !returns_void && WireBundle<value_return>;
    static constexpr bool ok = args_are_circuit && callable && !ambiguous &&
                               !returns_ref && !returns_void && returns_circuit;
};

// Single diagnostic site: instantiate `(void)sizeof(circuit_contract<Tr>)` in
// each entry point, paired with `if constexpr (Tr::ok)`. The guarded conditions
// make exactly the relevant message fire per failure mode.
template <class Tr, bool Ok = Tr::ok>
struct circuit_contract {};   // Ok == true: no diagnostics
template <class Tr>
struct circuit_contract<Tr, false> {
    static_assert(Tr::args_are_circuit,
        "frontend circuit: every argument must be a circuit value (Bit / UInt / Int / Float / ...)");
    static_assert(!Tr::ambiguous,
        "frontend circuit: body is callable in BOTH implicit and explicit context forms; "
        "disambiguate (take the context explicitly, or not at all)");
    static_assert(Tr::callable,
        "frontend circuit: body is not callable with prvalue circuit-value arguments — write "
        "[](auto a, auto b){...} or [](auto& ctx, auto a, auto b){...}, taking arguments by value");
    static_assert(!(Tr::callable && !Tr::ambiguous && Tr::returns_ref),
        "frontend circuit must RETURN BY VALUE: the output is a fresh circuit value, not a reference");
    static_assert(!(Tr::callable && !Tr::ambiguous && Tr::returns_void),
        "frontend circuit must return a circuit value, not void");
    static_assert(!(Tr::callable && !Tr::ambiguous && !Tr::returns_void) || Tr::returns_circuit,
        "frontend circuit must return a circuit value (Bit / UInt / Int / Float / ...), not a plain value");
};

// ---------------------------------------------------------------------------
// Circuit<RetV, ArgVs...> — a compiled, context-free circuit: a validated
// BooleanProgram + its signature (arg widths + return width). The value types
// (over RecordCtx) are template parameters so run() reconstructs the typed
// values via rebind<Ctx>.
// ---------------------------------------------------------------------------
// A compiled circuit's signature is named by value types over RecordCtx (the
// rec:: aliases), so the class requires RecordValue — only compile()/load helpers
// that normalize to rec:: types can name a Circuit.
template <RecordValue RetV, RecordValue... ArgVs>
class Circuit {
public:
    using ret_value = RetV;
    static constexpr std::size_t arity = sizeof...(ArgVs);

    // Construct from a recorded (or loaded) artifact. Validates the program
    // structurally AND that the signature matches the declared value types — so a
    // stale, hand-edited, or mis-typed loaded artifact is rejected here rather
    // than silently mis-running. The artifact is otherwise immutable (private).
    explicit Circuit(circuit::CircuitArtifact a) : artifact_(std::move(a)) {
        circuit::validate_artifact(artifact_);
        const circuit::CircuitSignature& s = artifact_.signature;
        const std::array<uint32_t, sizeof...(ArgVs)> want{(uint32_t)ArgVs::width()...};
        expecting(s.arg_widths.size() == sizeof...(ArgVs),
                  "Circuit: artifact signature arity != declared value types");
        for (std::size_t i = 0; i < want.size(); ++i)
            expecting(s.arg_widths[i] == want[i],
                      "Circuit: artifact argument width != declared value width");
        expecting(s.return_width == (uint32_t)RetV::width(),
                  "Circuit: artifact return width != declared value width");
    }

    const circuit::BooleanProgram&   program()   const { return artifact_.program; }
    const circuit::CircuitSignature& signature() const { return artifact_.signature; }

private:
    circuit::CircuitArtifact artifact_;
};

struct invalid_circuit_fn {};   // sentinel for the contract-violating branch

template <class T> inline constexpr bool is_circuit_v = false;
template <RecordValue R, RecordValue... A> inline constexpr bool is_circuit_v<Circuit<R, A...>> = true;

// ---------------------------------------------------------------------------
// compile<ArgVs...>(body): record the body once through a RecordCtx. The ArgVs
// are circuit value types over RecordCtx (use the rec:: aliases, e.g.
// rec::UInt<32>).
// ---------------------------------------------------------------------------
namespace detail {
template <bool Ok, class Tr, class... ArgVs>
struct compiled_ret { using type = invalid_circuit_fn; };
template <class Tr, class... ArgVs>
struct compiled_ret<true, Tr, ArgVs...> {
    using type = Circuit<typename Tr::value_return, ArgVs...>;
};
// Gate on RecordValue (args AND the body's return) so a non-RecordCtx argument
// makes the return type fall back to invalid_circuit_fn — keeping compile()'s
// signature well-formed so the in-body static_assert fires the clear diagnostic
// (rather than a substitution failure from an ill-formed Circuit<...>).
template <class Tr, class... ArgVs>
using compiled_ret_t =
    typename compiled_ret<((RecordValue<ArgVs> && ...) && Tr::ok &&
                           RecordValue<typename Tr::value_return>), Tr, ArgVs...>::type;

// Build one recording-context arg from a freshly reserved input window.
template <WireBundle RecV>
RecV make_rec_arg(RecordCtx& rc) {
    RecordCtx::Wire base = rc.external_input((size_t)RecV::width());
    std::array<RecordCtx::Wire, (std::size_t)RecV::width()> win{};
    for (int i = 0; i < RecV::width(); ++i) win[i] = base + (RecordCtx::Wire)i;
    return RecV::from_wires(rc, win.data());
}
}  // namespace detail

template <class... ArgVs, class F,
          class Tr = circuit_fn_traits<RecordCtx, std::decay_t<F>, ArgVs...>>
detail::compiled_ret_t<Tr, ArgVs...> compile(F&& body) {
    static_assert((RecordValue<ArgVs> && ...),
        "compile: each arg must be a circuit value over RecordCtx (use rec::UInt<32> / rec::Bit / ...)");
    (void)sizeof(circuit_contract<Tr>);
    if constexpr ((RecordValue<ArgVs> && ...) && Tr::ok) {
        static_assert(RecordValue<typename Tr::value_return>,
            "compile: the body must return a circuit value over RecordCtx");
        RecordCtx rc;
        // Reserve inputs + build args, left-to-right (braced-init order), BEFORE
        // any gate (RecordCtx requires all external_input calls up front).
        std::tuple<ArgVs...> args{detail::make_rec_arg<ArgVs>(rc)...};
        auto ret = [&] {
            if constexpr (Tr::wants_ctx)
                return std::apply([&](auto&&... a) { return body(rc, std::move(a)...); }, args);
            else
                return std::apply([&](auto&&... a) { return body(std::move(a)...); }, args);
        }();
        using Ret = std::decay_t<decltype(ret)>;
        std::array<RecordCtx::Wire, (std::size_t)Ret::width()> ow{};
        ret.pack_wires(ow.data());
        rc.finish(std::span<const RecordCtx::Wire>(ow.data(), ow.size()));

        circuit::CircuitArtifact art;
        art.program   = std::move(rc.prog);
        art.signature = circuit::CircuitSignature{
            std::vector<uint32_t>{(uint32_t)ArgVs::width()...}, (uint32_t)Ret::width()};
        return detail::compiled_ret_t<Tr, ArgVs...>(std::move(art));   // ctor validates
    } else {
        return {};   // invalid_circuit_fn; unreachable after the asserts
    }
}

// compile_at_<Level, ArgVs...>(body): like compile(), but the recorded program is
// run through make_compact at the given WireReuse level, so the resulting Circuit
// replays with a smaller wire array. Shared implementation behind compile_linear.
template <circuit::WireReuse Level, class... ArgVs, class F,
          class Tr = circuit_fn_traits<RecordCtx, std::decay_t<F>, ArgVs...>>
detail::compiled_ret_t<Tr, ArgVs...> compile_at_(F&& body) {
    auto dense = compile<ArgVs...>(std::forward<F>(body));   // dense Circuit (diagnostics fire here)
    if constexpr ((RecordValue<ArgVs> && ...) && Tr::ok) {
        circuit::CircuitArtifact art;
        art.program   = circuit::make_compact(dense.program(), Level).prog;
        art.signature = dense.signature();
        return detail::compiled_ret_t<Tr, ArgVs...>(std::move(art));   // ctor validates
    } else {
        return {};
    }
}

// compile_linear<ArgVs...>(body): WireReuse::Linear (AND outputs pinned). This is
// the form to use for a UNIT that will be composed (called via run() inside a
// run_compose body) on a multi-pass backend (ag2pc): Linear is what its trust path consumes.
template <class... ArgVs, class F>
auto compile_linear(F&& body) { return compile_at_<circuit::WireReuse::Linear, ArgVs...>(std::forward<F>(body)); }

// ---------------------------------------------------------------------------
// run(ctx, circuit, args...): use a compiled circuit on a live context. On most
// contexts it INLINES (replays the gates here). On a ComposeCtx it instead records
// the circuit as an OPAQUE instance (its wiring only) — so the SAME body composes
// units when recorded for a flattening backend (ag2pc run_compose) and inlines
// everywhere else. There is no separate "invoke": run IS the verb, opaque or inline
// by context, exactly like every other context-polymorphic op. Args by const-ref.
// ---------------------------------------------------------------------------
namespace detail {
template <class Wire, class V>
inline void append_wires(std::vector<Wire>& out, const V& v) {
    std::size_t base = out.size();
    out.resize(base + (std::size_t)V::width());
    v.pack_wires(out.data() + base);
}
}  // namespace detail

template <BooleanContext Ctx, WireBundle RetV, WireBundle... ArgVs>
typename RetV::template rebind<Ctx>
run(Ctx& ctx, const Circuit<RetV, ArgVs...>& c,
    const typename ArgVs::template rebind<Ctx>&... args) {
    using Wire = typename Ctx::Wire;
    const circuit::BooleanProgram& p = c.program();
    expecting(c.signature().arg_widths.size() == sizeof...(ArgVs),
              "frontend::run: argument count != circuit arity (stale artifact?)");
    std::vector<Wire> inputs;
    inputs.reserve(p.num_inputs);
    (detail::append_wires(inputs, args), ...);
    expecting((uint32_t)inputs.size() == p.num_inputs,
              "frontend::run: total argument width != circuit input count");
    if constexpr (std::same_as<std::remove_cvref_t<Ctx>, ComposeCtx>) {
        // Composition: record one opaque instance (wiring only), don't inline.
        std::vector<Wire> out = ctx.call_unit(p, inputs.data(), inputs.size());
        return RetV::template rebind<Ctx>::from_wires(ctx, out.data());
    } else {
#if EMP_CONTEXT_CHECKS
        // Every argument must belong to the context it is being replayed on.
        { bool ok = true; ((ok = ok && args.context() == &ctx), ...);
          expecting(ok, "frontend::run: an argument belongs to a different context"); }
#endif
        ProgramWorkspace<Wire> ws;
        const std::vector<Wire>& ow =
            execute_program(ctx, p, std::span<const Wire>(inputs.data(), inputs.size()), ws);
        return RetV::template rebind<Ctx>::from_wires(ctx, ow.data());
    }
}

// ---------------------------------------------------------------------------
// Live run(body, args...): evaluate a body directly on already-live typed values
// (no compile step). Recovers the context from the first argument, so it serves
// both body forms. (Nullary/explicit-only bodies: call the body directly.)
// ---------------------------------------------------------------------------
template <class F, class Arg0, class... Args,
          class Ctx = typename std::decay_t<Arg0>::context_type,
          class Tr  = circuit_fn_traits<Ctx, std::decay_t<F>, std::decay_t<Arg0>, std::decay_t<Args>...>,
          std::enable_if_t<!is_circuit_v<std::decay_t<F>>, int> = 0>
std::conditional_t<Tr::ok, typename Tr::value_return, invalid_circuit_fn>
run(F&& body, Arg0&& a0, Args&&... rest) {
    (void)sizeof(circuit_contract<Tr>);
    if constexpr (Tr::ok) {
        Ctx& ctx = *a0.context();
#if EMP_CONTEXT_CHECKS
        // All arguments must share the first argument's context.
        { bool ok = true; ((ok = ok && rest.context() == a0.context()), ...);
          expecting(ok, "frontend::run: arguments belong to different contexts"); }
#endif
        if constexpr (Tr::wants_ctx)
            return body(ctx, std::forward<Arg0>(a0), std::forward<Args>(rest)...);
        else
            return body(std::forward<Arg0>(a0), std::forward<Args>(rest)...);
    } else {
        return {};
    }
}

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_CIRCUIT_FN_H__
