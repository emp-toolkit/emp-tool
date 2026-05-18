# Static initialization safety

Read this before adding any file-scope `const` / `static` / `inline const`
data of type `block`, `__m128i`, or any other type whose initializer is
not a *constant expression*.

## The hazard

Consider a `block` constant built from an intrinsic:

```cpp
inline const block all_one_block = _mm_set_epi64x(~0, ~0);
```

`_mm_set_epi64x` is not a `constexpr` function, so `all_one_block` is
*dynamically* initialized at program start — the runtime stores into
it some time before `main()`.

C++ guarantees that, **within a single translation unit**, dynamic
initialization runs in declaration order. **Across translation units
it does not.** So if some other TU has:

```cpp
// in a different header included by other.cpp:
const static block lookup[] = {zero_block, all_one_block};
```

and *that* TU's dynamic initializer happens to run first (the link
order chosen for a particular binary decides), `lookup[1]` copies the
still-zero default block. From that point on `lookup[1]` is
permanently `00…00`, not the `ff…ff` you wrote.

The cross-TU order is per-binary and deterministic. So a binary built
with a "lucky" link order works perfectly; another binary built from
the **same source code** with a different link order produces the
wrong value forever. No race, no flake, no Heisenbug — just one
binary is silently broken and the others are silently fine.

This is the [static initialization order
fiasco](https://en.cppreference.com/w/cpp/language/siof). The
downstream symptom can be arbitrarily far from the cause — a constant
silently zero in one binary corrupts whatever consumes it, possibly
several layers removed.

## The fix

`makeBlock` is `constexpr`:

```cpp
inline constexpr block makeBlock(uint64_t high, uint64_t low) {
    return block{(long long)low, (long long)high};
}
```

`block` is a GCC/Clang vector type and aggregate-init `{lo, hi}` on a
vector type IS a constant expression — it compiles to the same byte
pattern as `_mm_set_epi64x(high, low)` (verified byte-identical on
x86 and aarch64-via-sse2neon). With `makeBlock` constexpr the
standard library constants compose cleanly:

```cpp
inline constexpr block zero_block    = makeBlock(0, 0);
inline constexpr block all_one_block = makeBlock(~0ULL, ~0ULL);
inline constexpr block select_mask[2] = {zero_block, all_one_block};
```

These live in `.rodata` — no dynamic initializer, no cross-TU order
question, no fiasco.

## Rules for new code

1. **Every new file-scope block constant uses `inline constexpr` and
   initializes via `makeBlock(...)` or aggregate-init.** Not
   `const block`, not `static const block`. The keyword matters: the
   compiler will tell you (with an error) if your initializer is not
   actually a constant expression.

2. **Same rule for any other type with a non-`constexpr`
   initializer.** If the type's constructor is dynamic — `Point`,
   `ECGroup`, `Hash`, `AES_KEY` (they all do real work) — *do not*
   put one at file scope and *do not* let another TU's file-scope
   global copy from it. Wrap it in a function-local static instead:

   ```cpp
   inline const block* foo() {
       struct Init { block buf[N]; Init() { /* fill */ } };
       static const Init holder;
       return holder.buf;
   }
   ```

   Function-local statics are initialized on first call, after all
   dynamic-init has had a chance to run, so they're SIOF-immune by
   construction. `emp-zk/ram-zk/gf_base.h:ramzk_gf_base()` is the
   existing example.

3. **When in doubt, prefer the function-local-static form.** Marginal
   cost is one branch on first call; marginal safety is total.

## Auditing existing constants

These file-scope block constants are `inline constexpr` and
SIOF-immune:

| File | Constant |
|---|---|
| `emp-tool/core/block.hpp` | `zero_block`, `all_one_block`, `select_mask[2]` |
| `emp-ot/ot_extension/cggm.h` | `kCggmLsbClearMask` |
| `emp-ot/ot_extension/ferret/constants.h` | `lsb_clear_mask`, `lsb_only_mask` |
| `emp-zk/emp-svole/fp_utility.h` | `prs`, `PRs` |
| `ref/emp-agmpc/helper.h` | `bit0_mask`, `bit1_mask` |

`grep -rn '^\(inline \|static \|const \)\+const block' .` over the
codebase finds candidates; anything that isn't `inline constexpr` is
suspect.

## Why not `__attribute__((init_priority))`?

`init_priority` orders dynamic initializers within one translation
unit. It does nothing across TUs unless every consumer also annotates,
and it's a GCC/Clang extension. Constant evaluation has none of those
drawbacks and is strictly more powerful — there is no init to order
if there is no dynamic init.
