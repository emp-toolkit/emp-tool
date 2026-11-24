#ifndef EMP_BLOCK_VECTOR_H__
#define EMP_BLOCK_VECTOR_H__
#include "emp-tool/runtime/core/block.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace emp {

// Allocations of at least this many bytes ask for transparent huge pages.
inline constexpr std::size_t kHugePageAdviceBytes = std::size_t{1} << 21;

// On Linux, mark [p, p + bytes) for transparent huge pages (MADV_HUGEPAGE) when it
// spans at least kHugePageAdviceBytes. Where the system allows them (its default
// "madvise" mode does), the kernel backs the range's 2 MiB-aligned parts with 2 MiB
// pages: one fault per 2 MiB the first time they are written instead of one per 4 KiB
// page, and fewer TLB misses. Elsewhere, or if the kernel refuses, nothing changes.
inline void advise_huge_pages(void *p, std::size_t bytes) noexcept {
#if defined(__linux__) && defined(MADV_HUGEPAGE)
    if (p == nullptr || bytes < kHugePageAdviceBytes) return;
    const long page_size = sysconf(_SC_PAGESIZE);
    const std::uintptr_t page = page_size > 0 ? (std::uintptr_t)page_size : 4096;
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(p);
    const std::uintptr_t start = begin & ~(page - 1);
    (void)madvise(reinterpret_cast<void *>(start), begin + bytes - start, MADV_HUGEPAGE);
#else
    (void)p;
    (void)bytes;
#endif
}

// Allocator that skips value-initialization for trivial types when
// vector::resize(N) / vector(N) would otherwise zero each element.
// Saves the bzero on memory that's about to be overwritten. Non-trivial
// types still go through their default ctor. An allocation of at least
// kHugePageAdviceBytes also asks for transparent huge pages
// (advise_huge_pages): such a buffer is usually written in full soon after,
// though one that is only partly touched can take more resident memory.
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

    typename a_t::pointer allocate(typename a_t::size_type n) {
        typename a_t::pointer p = a_t::allocate(static_cast<A &>(*this), n);
        advise_huge_pages(std::to_address(p), n * sizeof(T));
        return p;
    }

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
