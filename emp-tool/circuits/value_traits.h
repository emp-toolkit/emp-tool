#ifndef EMP_CIRCUIT_VALUE_TRAITS_H__
#define EMP_CIRCUIT_VALUE_TRAITS_H__
// Uniform metadata accessor for context-bound circuit values. Source of truth =
// the value type's own static members; this trait just exposes them uniformly
// (width, clear codec, rebind<Ctx>) for circuits-layer call sites
// (compile()/run() signature deduction). Session I/O code reads the value's
// members directly (V::width()/encode/decode — see the SessionIO concept in
// ir/session/session_io.h); ir/ never includes this header.
#include "emp-tool/ir/context/concept.h"   // BooleanContext
#include <vector>
namespace emp {
template <class T>
struct value_traits {
    using clear_t = typename T::clear_t;
    static constexpr int width() { return T::width(); }
    static auto encode(const clear_t& v) { return T::encode(v); }
    static clear_t decode(const bool* b) { return T::decode(b); }
    template <BooleanContext Ctx> using rebind = typename T::template rebind<Ctx>;
};
}  // namespace emp
#endif
