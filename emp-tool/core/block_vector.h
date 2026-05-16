#ifndef EMP_BLOCK_VECTOR_H__
#define EMP_BLOCK_VECTOR_H__
#include "emp-tool/core/block.h"
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace emp {

// Allocator that skips value-initialization for trivial types when
// vector::resize(N) / vector(N) would otherwise zero each element.
// Saves the bzero on memory that's about to be overwritten. Non-trivial
// types still go through their default ctor.
template <typename T, typename A = std::allocator<T>>
class default_init_allocator : public A {
    using a_t = std::allocator_traits<A>;
public:
    template <typename U>
    struct rebind {
        using other = default_init_allocator<U,
            typename a_t::template rebind_alloc<U>>;
    };
    using A::A;

    // Default-init: no-op for trivial T (no zero-fill).
    template <typename U>
    void construct(U *ptr)
        noexcept(std::is_nothrow_default_constructible<U>::value) {
        ::new (static_cast<void *>(ptr)) U;
    }
    // vector(N, val), emplace_back(args...): forward to base allocator.
    template <typename U, typename Arg, typename... Args>
    void construct(U *ptr, Arg &&arg, Args &&...args) {
        a_t::construct(static_cast<A &>(*this), ptr,
                       std::forward<Arg>(arg),
                       std::forward<Args>(args)...);
    }
};

template <class T>
using default_init_vector = std::vector<T, default_init_allocator<T>>;

using BlockVec = default_init_vector<block>;

} // namespace emp
#endif
