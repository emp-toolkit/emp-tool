#ifndef EMP_CONTEXT_CHECKS_H__
#define EMP_CONTEXT_CHECKS_H__

// Debug cross-context guard shared by every typed circuit value. Mixing typed
// values from two different contexts (sessions/executors) silently corrupts —
// especially with id-based wires. The check defaults to DEBUG-ONLY (on when
// NDEBUG is unset). Opt in to a hard, always-on error() in release with
// -DEMP_CONTEXT_CHECKS=1; force it off with -DEMP_CONTEXT_CHECKS=0. When enabled
// it raises error() (not just assert), so it survives NDEBUG once opted in.

#include "emp-tool/runtime/core/utils.h"   // error()

namespace emp {

#ifndef EMP_CONTEXT_CHECKS
#  ifndef NDEBUG
#    define EMP_CONTEXT_CHECKS 1
#  else
#    define EMP_CONTEXT_CHECKS 0
#  endif
#endif

template <class A, class B>
inline void check_same_context(const A& l, const B& r) {
#if EMP_CONTEXT_CHECKS
    // A null context means a default-constructed (uninitialized) value reached an
    // operator — catch it clearly instead of dereferencing null. (Two defaults
    // would otherwise both be null and "match".)
    expecting(l.context() && r.context(),
              "typed value: operand is uninitialized (default-constructed, no context)");
    expecting(l.context() == r.context(),
              "typed value: operands belong to different contexts");
#else
    (void)l; (void)r;
#endif
}

}  // namespace emp
#endif  // EMP_CONTEXT_CHECKS_H__
