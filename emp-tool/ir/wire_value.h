#ifndef EMP_IR_WIRE_VALUE_H__
#define EMP_IR_WIRE_VALUE_H__

// The two structural contracts for a typed value object over a BooleanContext.
//
// WireBundle — what circuit EXECUTION needs: a fixed-width bundle of wires,
// packable to / constructible from raw Ctx::Wire, rebindable to another
// context, and reporting its own context pointer. The frontend (compile / run)
// and IR replay constrain on this and nothing more — a value with no clear
// codec is still a perfectly good circuit argument / return.
//
// WireValue — WireBundle plus the clear-value codec (`clear_t` / `encode` /
// `decode`): what session I/O needs to feed and open a value. Sessions
// (SessionIO, input / reveal) are the only WireValue consumers.
//
// Neither names a concrete value family (Bit/UInt/Int/Float/BitVec) or a
// protocol. A value type satisfies them through its own static members —
// `Wire`/`context_type`/`width()`/`rebind`/`pack_wires`/`from_wires`/
// `context()` (bundle) + `clear_t`/`encode`/`decode` (codec) — read uniformly
// through value_traits<T> (circuits/value_traits.h) where a metadata accessor
// is wanted.
//
// POSITIVE FIXED-WIDTH ONLY: WireBundle requires a static `width() > 0`, so a
// runtime-width value (e.g. UInt_T<Ctx, runtime_width>) and a zero-bit internal
// helper model neither concept. Session I/O and circuit compilation need a
// statically known, nonempty width / signature. Runtime-width values remain
// usable in-circuit and through the separate RuntimeSessionIO surface.
//
// The codec may also be narrower than the bundle: UInt_T/Int_T carry their
// clear value in a 64-bit codec, so UInt_T<Ctx,128> is a WireBundle (usable as
// a circuit argument) but NOT a WireValue (no 128-bit clear_t; use
// BitVec_T<Ctx,128> for typed session I/O at that width).

#include "emp-tool/ir/context/concept.h"   // BooleanContext
#include <array>
#include <concepts>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace emp {

template <class V_>
concept WireBundle =
    requires {
        typename std::decay_t<V_>::Wire;
        typename std::decay_t<V_>::context_type;
    } &&
    // The value lives over a real gate context, and its wire IS that context's wire.
    BooleanContext<typename std::decay_t<V_>::context_type> &&
    std::same_as<typename std::decay_t<V_>::Wire,
                 typename std::decay_t<V_>::context_type::Wire> &&
    // width() is a static, compile-time constant int — so a runtime-width value
    // (whose width() is `requires (N > 0)` and absent at N == 0) is not a WireBundle.
    requires { typename std::integral_constant<int, std::decay_t<V_>::width()>; } &&
    requires { requires (std::decay_t<V_>::width() > 0); } &&
    std::same_as<typename std::decay_t<V_>::template rebind<typename std::decay_t<V_>::context_type>,
                 std::decay_t<V_>> &&
    requires(const std::decay_t<V_> v, typename std::decay_t<V_>::Wire* out) {
        { v.context() } -> std::convertible_to<typename std::decay_t<V_>::context_type*>;
        v.pack_wires(out);
    } &&
    requires(typename std::decay_t<V_>::context_type& c, const typename std::decay_t<V_>::Wire* in) {
        { std::decay_t<V_>::from_wires(c, in) } -> std::same_as<std::decay_t<V_>>;
    };

template <class V_, class Ctx>
using wire_bundle_rebind_t =
    typename std::decay_t<V_>::template rebind<std::remove_cvref_t<Ctx>>;

// The cross-context half of the WireBundle contract. WireBundle itself checks
// that rebinding to the current context is stable; frontend replay uses this
// refinement when it moves a recorded value family onto a live context.
template <class V_, class Ctx>
concept RebindableWireBundle =
    WireBundle<V_> &&
    BooleanContext<std::remove_cvref_t<Ctx>> &&
    requires { typename wire_bundle_rebind_t<V_, Ctx>; } &&
    WireBundle<wire_bundle_rebind_t<V_, Ctx>> &&
    std::same_as<typename wire_bundle_rebind_t<V_, Ctx>::context_type,
                 std::remove_cvref_t<Ctx>> &&
    (wire_bundle_rebind_t<V_, Ctx>::width() == std::decay_t<V_>::width());

// Codec storage follows docs/api_conventions.md's bit-buffer contract:
// fixed-width typed values encode to std::array<bool, width()>.
template <class V_>
concept WireValue =
    WireBundle<V_> &&
    requires { typename std::decay_t<V_>::clear_t; } &&
    requires(typename std::decay_t<V_>::clear_t cv, const bool* bits) {
        { std::decay_t<V_>::encode(cv) }
            -> std::same_as<std::array<bool, (std::size_t)std::decay_t<V_>::width()>>;
        { std::decay_t<V_>::decode(bits) } -> std::same_as<typename std::decay_t<V_>::clear_t>;
    };

template <class V> inline constexpr bool is_wire_value_v = WireValue<std::decay_t<V>>;

// RuntimeWidthValue — the runtime-width sibling of WireValue. Width lives in the
// object (a runtime int, not the type), the clear codec rides BYTE-BOOLS (uint8_t,
// value 0/1) in a runtime-sized std::vector, and the value is SESSION-I/O eligible
// but NOT compile/run eligible: it has no static width(), so it models neither
// WireBundle nor WireValue and never enters cf::compile / IR replay. The two
// instances today are UInt_T<Ctx, runtime_width> and Int_T<Ctx, runtime_width>
// (runtime_width == the N == 0 mode). Sessions must copy the byte-bools into real
// `bool` storage at the engine boundary — never reinterpret uint8_t* as bool*.
template <class V_>
concept RuntimeWidthValue =
    requires {
        typename std::decay_t<V_>::Wire;
        typename std::decay_t<V_>::context_type;
        typename std::decay_t<V_>::clear_t;
    } &&
    BooleanContext<typename std::decay_t<V_>::context_type> &&
    std::same_as<typename std::decay_t<V_>::Wire,
                 typename std::decay_t<V_>::context_type::Wire> &&
    requires { requires std::decay_t<V_>::is_dynamic == true; } &&
    requires(const std::decay_t<V_> v, typename std::decay_t<V_>::Wire* out,
             typename std::decay_t<V_>::context_type& c,
             const typename std::decay_t<V_>::Wire* in,
             typename std::decay_t<V_>::clear_t cv, const uint8_t* bits, int width) {
        { v.width() } -> std::same_as<int>;
        { v.context() } -> std::convertible_to<typename std::decay_t<V_>::context_type*>;
        v.pack_wires(out);
        { std::decay_t<V_>::from_wires(c, in, width) } -> std::same_as<std::decay_t<V_>>;
        { std::decay_t<V_>::encode(cv, width) } -> std::same_as<std::vector<uint8_t>>;
        { std::decay_t<V_>::decode(bits, width) } -> std::same_as<typename std::decay_t<V_>::clear_t>;
    };

template <class V> inline constexpr bool is_runtime_width_value_v = RuntimeWidthValue<std::decay_t<V>>;

}  // namespace emp
#endif  // EMP_IR_WIRE_VALUE_H__
