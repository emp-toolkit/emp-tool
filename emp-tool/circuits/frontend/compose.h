#ifndef EMP_FRONTEND_COMPOSE_H__
#define EMP_FRONTEND_COMPOSE_H__

// Recording a COMPOSITION. There is no new call verb: you compose by writing an
// ordinary body that uses run(ctx, unit, args) to call pre-compiled units, wired
// however you like (chain, SIMD, tree, heterogeneous). run() is opaque on a
// ComposeCtx (records the unit as a referenced instance, O(width)) and inline on
// every other context (so the same body inlines on a plaintext/ZK context as its
// own oracle — see circuits/frontend/circuit_fn.h). compose() records such a body
// over a ComposeCtx into a ComposePlan (O(#glue gates + #instances); the units are
// never inlined here), which a flattening backend (emp-ag2pc run_compose) replays
// on the fly. Units must be WireReuse::Linear for a multi-pass backend (compile_linear).

#include "emp-tool/circuits/frontend/circuit_fn.h"   // Circuit, run, RecordValue
#include "emp-tool/ir/context/compose.h"             // ComposeCtx, ComposePlan
#include <array>
#include <span>
#include <tuple>
#include <type_traits>

namespace emp {
namespace frontend {
namespace detail {
// Reserve one external-input window on a ComposeCtx and build the typed arg.
template <class CV>   // CV is a value type over ComposeCtx
CV make_compose_arg(ComposeCtx& cc) {
    ComposeCtx::Wire base = cc.external_input((size_t)CV::width());
    std::array<ComposeCtx::Wire, (std::size_t)CV::width()> win{};
    for (int i = 0; i < CV::width(); ++i) win[i] = base + (ComposeCtx::Wire)i;
    return CV::from_wires(cc, win.data());
}
}  // namespace detail

// Record a composition body (explicit-ctx form: [](auto& ctx, args...){ ... run(ctx, unit, ...) ... })
// over a ComposeCtx, returning the ComposePlan. ArgVs are rec:: value types (over
// RecordCtx), exactly like compile<>.
template <class... ArgVs, class F>
ComposePlan compose(F&& body) {
    static_assert((RecordValue<ArgVs> && ...),
        "compose: each arg must be a circuit value over RecordCtx (use rec::UInt<32> / rec::Bit / ...)");
    ComposeCtx cc;
    std::tuple<typename ArgVs::template rebind<ComposeCtx>...> args{
        detail::make_compose_arg<typename ArgVs::template rebind<ComposeCtx>>(cc)...};
    auto ret = std::apply([&](auto&&... a) { return body(cc, std::move(a)...); }, args);
    using Ret = std::decay_t<decltype(ret)>;
    std::array<ComposeCtx::Wire, (std::size_t)Ret::width()> ow{};
    ret.pack_wires(ow.data());
    cc.finish(std::span<const ComposeCtx::Wire>(ow.data(), ow.size()));
    return std::move(cc.plan);
}

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_COMPOSE_H__
