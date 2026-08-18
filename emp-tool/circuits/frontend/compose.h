#ifndef EMP_FRONTEND_COMPOSE_H__
#define EMP_FRONTEND_COMPOSE_H__

// Record a body that calls compiled units with run(ctx, unit, args) into a
// ComposePlan. ComposeCtx stores each unit reference and its wiring; other
// contexts inline the same body normally. Backends decide which WireReuse modes
// they support when consuming the plan.

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
    for (int i = 0; i < CV::width(); ++i)
        win[i] = base + (ComposeCtx::Wire)i;
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
    static_assert((RebindableWireBundle<ArgVs, ComposeCtx> && ...),
        "compose: every argument value family must rebind to ComposeCtx at the same width");

    using Tr = circuit_fn_traits<
        ComposeCtx, std::decay_t<F>, wire_bundle_rebind_t<ArgVs, ComposeCtx>...>;
    (void)sizeof(circuit_contract<Tr>);
    static_assert(Tr::wants_ctx,
        "compose: body must take ComposeCtx& as its first argument");

    if constexpr ((RecordValue<ArgVs> && ...) &&
                  (RebindableWireBundle<ArgVs, ComposeCtx> && ...) &&
                  Tr::ok && Tr::wants_ctx) {
        ComposeCtx cc;
        std::tuple<wire_bundle_rebind_t<ArgVs, ComposeCtx>...> args{
            detail::make_compose_arg<wire_bundle_rebind_t<ArgVs, ComposeCtx>>(cc)...};
        auto ret = detail::invoke_circuit_body<Tr>(body, cc, args);
        expecting(ret.context() == &cc,
                  "compose: body returned a value owned by a different context");
        using Ret = std::decay_t<decltype(ret)>;
        std::array<ComposeCtx::Wire, (std::size_t)Ret::width()> ow{};
        ret.pack_wires(ow.data());
        cc.finish(std::span<const ComposeCtx::Wire>(ow.data(), ow.size()));
        return std::move(cc.plan);
    } else {
        return {};
    }
}

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_COMPOSE_H__
