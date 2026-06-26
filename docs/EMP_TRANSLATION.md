# Translating C++ / Python to EMP secure circuits

A reference for AI agents asked to take ordinary code and produce a
boolean-circuit equivalent that runs on **emp-tool**'s circuit layer. You
write the logic over the context-bound typed values
(`Bit_T<Ctx>` / `BitVec_T<Ctx,N>` / `UInt_T<Ctx,N>` / `Int_T<Ctx,N>` /
`Float_T<Ctx,W>`, [typed.h](../emp-tool/circuits/typed.h)) — usually as a
`template<class Ctx>` circuit body — and the same code runs on any
`BooleanContext`: `ClearCtx` (plaintext), the garbled `SH2PCCtx`, etc.

If you read only one section, read **§1 The hard rule** and **§4
Translation patterns**. The rest is type detail.

This doc covers *writing* the circuit logic. To *run* it as a reusable
function — call it live, or compile it once and replay it on any context —
see [frontend.md](frontend.md).

---

## 0. Mental model in three sentences

EMP evaluates a fixed boolean circuit (AND / XOR / NOT gates over wires)
once. Each input is owned by exactly one party — `ALICE`, `BOB`, or
`PUBLIC` (everyone agrees) — and each output is revealed to a chosen
party. The same circuit body runs on every `BooleanContext` — `ClearCtx`
for plaintext / cost analysis, the garbled `SH2PCCtx` (from emp-sh2pc) for
semi-honest 2PC, and other protocol contexts — with nothing about the
circuit changing per context.

---

## 1. The hard rule

> **No control flow may depend on a secret value.**

Everything else in this doc is a consequence. A secret is anything fed
with `party == ALICE` or `party == BOB`, or anything derived from one.

Concretely, in EMP you may never write:

```cpp
if (secret_bit) { ... }              // host-language if on a secret
while (secret_int < 10) { ... }      // loop bound depends on secret
arr[secret_index]                    // memory access at secret address
return secret_value;                 // early-exit branch
```

The circuit is *static*. Both branches of every conditional must be
evaluated; the choice between them happens via an oblivious mux
(`a.select(sel, b)` returns `sel ? b : a`). Loops must have public bounds.
Arrays at secret indices must be linearly enumerated. See §4.

If your input C++ / Python relies on early exit or data-dependent loop
termination, **stop and either find a public worst-case bound, or
report that the program cannot be translated as-written**.

---

## 2. Type mapping

The widths `N` / `W` are template parameters, so each source width maps to a
distinct C++ type. `Ctx` is the `BooleanContext` the body is written over.

| Source type                       | EMP type                              | Notes |
|-----------------------------------|----------------------------------------|---|
| `bool`                            | `Bit_T<Ctx>`                           | clear type `bool` |
| `uint8_t … uint64_t`, `unsigned`  | `UInt_T<Ctx,N>`                        | N = bit width; wraps mod 2^N |
| `int8_t … int64_t`, `int`         | `Int_T<Ctx,N>`                         | two's complement |
| `float` (IEEE 754 binary32)       | `Float_T<Ctx,32>`                      | also `Float_T<Ctx,16>` / `<Ctx,64>` |
| `double` (IEEE 754 binary64)      | `Float_T<Ctx,64>`                      | correctly-rounded; same op set |
| fixed-size bit array / packed flags | `UInt_T<Ctx,N>` (use `& \| ^ ~`)     | bitwise ops, shifts/rotates, `slice`/`concat` |
| Python `int` (arbitrary precision)| `Int_T<Ctx,N>` after **you** pick N    | translator must commit to a width |
| Python `bool`                     | `Bit_T<Ctx>`                           | |
| Python `float`                    | `Float_T<Ctx,32>`                      | |
| `std::string`, `bytes`, `bytearray` | array of `UInt_T<Ctx,8>` of fixed length | length is public; pad to max |
| `std::vector<T>` (fixed size)     | `std::vector<EMPType>` of public length | |
| `std::vector<T>` (secret size)    | **not supported** — pad to public max length and carry a `valid` `Bit_T<Ctx>` per slot |
| `std::map`, `std::unordered_map`  | **not supported** — flatten to parallel arrays + linear scan |
| `std::optional<T>`                | `pair<T, Bit_T<Ctx> valid>`            | |
| pointers / references to host data| not represented; only the *value* matters |

The value types are templated on a `BooleanContext`. In a reusable circuit
body, leave `Ctx` generic (`template<class Ctx>` or a lambda taking
`auto`-typed arguments) so the same body `compile`s and `run`s on any context;
when you operate on already-live values, `Ctx` is the concrete context type
(`ClearCtx`, `SH2PCCtx`, …).

### 2.1. Width is a type parameter

The width is part of the type — there is no run-time width argument. Make a
public constant with `T::constant(ctx, v)` (or `a.constant(v)` from an
existing value of the same family/context):

```cpp
auto a = Int_T<ClearCtx,32>::constant(cx, -7);
auto b = UInt_T<ClearCtx,64>::constant(cx, 0xDEADBEEFull);
```

When translating, pick N from the source type:

* `int32_t` → 32, `int64_t` → 64, `int` → 32 (assuming LP64), `size_t`
  → 64 on 64-bit hosts, etc.
* Python `int`: pick the smallest power-of-two width that holds the
  range used in the program. If unclear, default to 64; document the
  choice in a comment.

### 2.2. Mixing widths

Operands of `+ - * / % & | ^ < <= > >= == !=` **must have equal width** — it
is enforced by the type (`UInt_T<Ctx,32>` and `UInt_T<Ctx,64>` are different
types). Decide widths up front and feed inputs at the matching width.

### 2.3. Mixing signed and unsigned

`Int_T<Ctx,N>` and `UInt_T<Ctx,N>` are different types. They share bit layout
and the low-N bits of `+`/`-`/`*` are identical; what differs is comparison
(`<` etc.), `>>` (arithmetic vs logical), and division/remainder sign
handling. Choose the type that matches the source's signedness; to
reinterpret a bit pattern, use `as_signed()` / `as_unsigned()` — same wires,
no gates.

---

## 3. Inputs, outputs, parties

I/O is the **session's** job, around the circuit body. A pure circuit body takes
its inputs as arguments and returns its output; it does no `input` / `reveal` of
its own. A session owns the I/O boundary (and the protocol state — IO, party,
preprocessing) over a pure context; the protocol library provides it, with
emp-tool's `ClearSession` as the reference. `sess` below is that session.

### 3.1. Feeding input values

```cpp
using Ctx = SH2PCSession::ctx_t;
using S32 = Int_T<Ctx, 32>;
using U32 = UInt_T<Ctx, 32>;
auto a = sess.input<S32>(ALICE,  alice_value);   // owned by Alice
auto b = sess.input<U32>(BOB,    bob_value);     // owned by Bob
auto k = sess.input<S32>(PUBLIC, 17);            // both parties agree on the literal
```

`sess.input<T>(owner, clear)` is called by both parties; the `clear` argument on a
non-`PUBLIC` owner is **only read on that party's process**. The other party passes
a dummy of the same type; the protocol routes the real bits through OT. A `PUBLIC`
constant inside a body uses `T::constant(sess.ctx(), v)` instead.

### 3.2. Revealing outputs

```cpp
uint64_t r = (uint64_t)sess.reveal(result, PUBLIC).value();  // both parties see it
auto     s = sess.reveal(result, ALICE);             // std::optional: value on Alice, nullopt on Bob
```

`sess.reveal(value, recipient)` returns `std::optional<clear_t>` and is the *only*
way to get a host-language value back from a circuit value: the value is present on
a party that learns it (every party for `PUBLIC`, the named recipient otherwise) and
`std::nullopt` on a party that does not — never a decoded placeholder. A plaintext
session (`ClearSession`) always populates it. Revealing a secret comparison to
`PUBLIC` **leaks that bit to both parties**. This is sometimes
intentional and sometimes a security bug — the translator must not insert reveals
it wasn't asked for.

### 3.3. Single binary

The same source file is compiled once and run as both parties (e.g.
`./prog 1 12345` for Alice, `./prog 2 12345` for Bob). The party identity is a
runtime int, typically read from `argv[1]`, and passed to the session
constructor (`SH2PCSession sess(&io, party)`). Both invocations call the same
`sess.input<T>(ALICE, …)` / `sess.input<T>(BOB, …)` — each process supplies its
own real value where it owns the input and a placeholder elsewhere.
Translators should preserve this symmetry.

---

## 4. Translation patterns

The oblivious mux is `base.select(sel, alt)` — it returns `alt` when `sel`
is true, `base` otherwise. (`sel` is a `Bit_T<Ctx>`; `base`/`alt` are the
same value type.) That single primitive expresses every conditional below;
both branches are always evaluated.

### 4.1. `if / else` on a secret value

```cpp
// C++ source:
int max = (a > b) ? a : b;

// EMP: b.select(a > b, a) == (a > b) ? a : b
auto max = b.select(a > b, a);
```

`a > b` returns `Bit_T<Ctx>`.

### 4.2. `if (cond) x = y;` (mutation form)

```cpp
// Source:
if (cond) x = y;

// EMP: identity on the false side
x = x.select(cond_bit, y);
```

### 4.3. Comparisons

`< <= > >= == !=` on `UInt_T`, `Int_T`, and `Float_T` return a
`Bit_T<Ctx>` — **not** host `bool`. You can only use one in host-language
`if`/`while` after a `reveal`, which leaks. Keep comparisons inside the
circuit by feeding the resulting `Bit` into `select`, `&`, `|`, `!`.

`UInt_T` comparisons are unsigned; `Int_T` comparisons are signed (the top
bit handles the sign). `Float_T` comparisons are NaN-aware (they replay the
`fp<W>_*` circuits).

### 4.4. Loops

Public bound, fully unrolled at "circuit construction" time:

```cpp
for (size_t i = 0; i < N; ++i) {        // N must be a compile-time
    acc = acc + arr[i];                  // or runtime-public constant
}
```

Secret bound — must convert to a public worst-case loop with masked
state:

```cpp
// Source:
//   while (n > 0) { acc += step(n); n--; }   // n is secret

// EMP, with public upper bound MAX:
auto zero = U32::constant(ctx, 0);
auto one  = U32::constant(ctx, 1);
Bit_T<Ctx> running = Bit_T<Ctx>::constant(ctx, true);
for (size_t i = 0; i < MAX; ++i) {
    Bit_T<Ctx> alive = running & (zero < n);              // n > 0
    auto next_acc = acc + step(n);
    acc = acc.select(alive, next_acc);
    n   = n.select(alive, n - one);
    running = alive;
}
```

If MAX is unknown, the program is not translatable.

### 4.5. Array read at a secret index

```cpp
// Source:
//   y = arr[idx];   // idx secret, arr public-length

// EMP: linear mux. Cost is O(len(arr)) in AND gates.
// (ctx is the body's BooleanContext parameter; at a call site it is sess.ctx().)
auto y = T::constant(ctx, 0);
for (size_t k = 0; k < arr.size(); ++k) {
    Bit_T<Ctx> eq = (idx == UIdx::constant(ctx, k));
    y = y.select(eq, arr[k]);
}
```

### 4.6. Array write at a secret index

```cpp
// arr[idx] = v;
for (size_t k = 0; k < arr.size(); ++k) {
    Bit_T<Ctx> eq = (idx == UIdx::constant(ctx, k));
    arr[k] = arr[k].select(eq, v);
}
```

Cost: O(len(arr)) per write. If you have many writes, restructure to
use one of the ORAM constructions in the wider literature; emp-tool
itself ships no ORAM.

### 4.7. Min / max over an array

```cpp
auto best = a[0];
for (size_t i = 1; i < a.size(); ++i)
    best = best.select(a[i] < best, a[i]);
```

### 4.8. Counting matches (popcount-style)

```cpp
auto count = U32::constant(ctx, 0);
auto one   = U32::constant(ctx, 1);
auto zero  = U32::constant(ctx, 0);
for (size_t i = 0; i < a.size(); ++i)
    count = count + zero.select(a[i] == target, one);
```

### 4.9. Bit-level packing / unpacking

Index `0` is the LSB everywhere in emp-tool. Operate bitwise (`& | ^ ~`) and
index single bits with `v[i]` (returns a `Bit_T<Ctx>`). Width changes and
splits/joins have first-class spellings — `slice<Lo,Hi>()` / `extract<B,W>()`
/ `concat(hi)` / `zext<M>()` (`sext<M>()` on `Int_T`) / `trunc<M>()` — all
pure wiring, no gates; raw `pack_wires` / `from_wires` remains for anything
those don't cover.

### 4.10. Sorting (oblivious)

A bitonic sort is a public network of compare-swaps, each
`(x, y) -> (min, max)` built from one comparison and two `select`s:

```cpp
auto swap = [](auto& x, auto& y) {        // ascending compare-swap
    Bit_T<Ctx> gt = x > y;
    auto lo = x.select(gt, y);
    auto hi = y.select(gt, x);
    x = lo; y = hi;
};
```

Drive it from a fixed (public) Batcher schedule. Cost is O(N log² N)
compare-swaps; each is O(W) gates.

### 4.11. Floats

`Float_T<Ctx,W>` is IEEE binary{16,32,64}. It supplies `+ - * / unary-`,
`sqr`, `sqrt`, `recip`, `rsqrt`, `fma`, `min`, `max`, `abs`, `copysign`,
classifiers (`is_nan`, `is_inf`, `is_zero`), and comparisons (`equal`,
`not_equal`, `less_than`, `less_equal`, `greater_than`, `greater_equal`, and
the `< <= > >= == !=` operators). Transcendentals (`sin`/`cos`/`exp`/`log`/…)
are not provided; use fixed-point or an explicit approximation circuit if a
source program needs them.

Float ops are large (thousands of AND gates for a single mul) — each
nontrivial op replays the recorded `fp<W>_<op>.empbc` builtin. For ML-style
fixed-point work, prefer `Int_T` with a chosen scale.

### 4.12. Crypto primitives (AES-128, SHA3-256)

Don't hand-roll AES or SHA3 in scalar `Bit_T`/integer ops. Use the
`emp::circuit::crypto` kernels over `BitVec_T` blocks/messages. emp-tool ships pre-built
circuits as native `.empbc` programs; load one and replay it on your context
(see [frontend.md](frontend.md) and the `.empbc` section of the README), or
`compile` a body that calls them. Each input/output bit is a wire — there are
no host-side crypto branches inside. Cost is 6400 ANDs for one AES-128 block,
24744 for one SHA-256 compression, 38400 for one Keccak-f permutation.

The BooleanContext-native kernels live in `emp::circuit::crypto`
(`circuits/crypto/aes128.h` / `sha256.h` / `keccak.h`). For ground-truth comparisons in
tests, use known published vectors or a `ClearCtx` replay of the same circuit.

---

## 5. Semantics that bite the translator

These match **hardware** two's-complement semantics, not C standard
"undefined behavior". A naive translator that reproduces the source's
UB-avoidance dance will produce extra gates for no reason.

* **Signed wrap**: `+`, `-`, `*` on `Int_T<Ctx,N>` wrap mod 2^N. This
  matches `int{N}_t` on x86 / arm. C calls signed overflow UB; emp-tool
  is deterministic. Don't insert `__builtin_add_overflow` checks.
* **Division by zero saturates**: `a / 0` and `a % 0` return the
  saturating result of the restoring-division circuit (it does **not**
  follow C's UB). The caller should still ensure the divisor is nonzero
  where the source program does; if the divisor is secret, guard with a
  `select` that picks a sentinel result and a "valid" `Bit_T<Ctx>`.
* **Signed `/` / `%`**: truncate toward zero; the remainder takes the
  dividend's sign (C99+). The most-negative operand to signed division is
  a UB precondition (as for `int{N}_t`).
* **Shift cost depends on the amount's visibility**: `<<` / `>>` by a
  public `int` amount is pure wiring — zero gates (`>>` on `Int_T` is
  arithmetic, sign-filling; on `UInt_T` logical). `<<` / `>>` by a
  *secret* amount is a barrel shifter: ceil(log2 N) mux levels, O(N log N)
  ANDs — don't use one when the source shift amount is actually public.
  `UInt_T` also has `rotl` / `rotr` (public amount, free). Width changes
  are explicit: compile-time `slice` / `extract` / `concat` / `zext`
  (`sext` on `Int_T`) / `trunc`, and runtime-width `resize`.
* **No `.abs` on `Int_T`**: compose it — `x.select(x < zero, -x)` —
  and mind that the most-negative value has no positive counterpart.
* **`==` / `!=` cost**: equal-test is cheap on `Bit_T` (one XOR), grows
  to a tree of XORs+ANDs on multi-bit types. `<` etc. are subtraction
  followed by a sign check, which is the dominant cost. Avoid
  redundant comparisons.
* **Unary `-` on `Int_T`**: two's-complement negate (`0 - x` mod 2^N).
  `UInt_T` has no unary minus; subtract from a zero constant if you need
  modular negation.

---

## 6. Running the circuit on a context

The circuit body is written over a `BooleanContext`; you pick the context at
the call site. I/O is the session's job, around the body — the session owns the
`input`/`reveal` boundary over a pure context.

### 6.1. Plaintext (for testing translations)

`ClearCtx` evaluates the circuit in cleartext — its `Wire` is the raw bit, so
it sees both parties' "secret" inputs (there is only one process). Use it to
verify a translation matches the original C++ / Python before standing up two
real parties:

```cpp
#include "emp-tool/emp-tool.h"
using namespace emp;

ClearSession sess;                       // owns a pure ClearCtx + the I/O boundary
using Ctx = ClearSession::ctx_t;
using U32 = UInt_T<Ctx, 32>;
auto a = sess.input<U32>(ALICE, av);     // plaintext: a session input is just a public wire
auto b = sess.input<U32>(BOB, bv);
auto c = a + b;
uint64_t r = sess.reveal(c, PUBLIC).value();  // results leave through the session
```

To count AND gates — the right metric for circuit cost (XOR / NOT are free in
modern garbling) — write the body over `CountCtx` (it tallies gate calls), or
count over a recorded `BooleanProgram` (`ir/passes.h`). To capture the
circuit as a native `.empbc` file, `compile` the body and serialize its
program (see [frontend.md](frontend.md)).

### 6.2. Semi-honest 2PC

For end-to-end semi-honest 2PC use **emp-sh2pc**'s `SH2PCSession`. It owns the
protocol state (IO / OT / Delta / PRG / half-gate) and exposes the same surface
as `ClearSession` — `input` / `reveal` for the I/O boundary and `ctx()` for
the garbled `SH2PCCtx` gate context that value construction and `frontend::run` use:

```cpp
NetIO io(party == ALICE ? nullptr : "127.0.0.1", port);
SH2PCSession sess(&io, party);                  // owns the protocol state + I/O boundary
using Ctx = SH2PCSession::ctx_t;
using U32 = UInt_T<Ctx, 32>;

auto a = sess.input<U32>(ALICE, av);                   // private inputs
auto b = sess.input<U32>(BOB,   bv);
auto k = U32::constant(sess.ctx(), kv);                // public constant
auto c = a + b + k;                                     // or frontend::run(sess.ctx(), circ, ...)
uint32_t r = (uint32_t)sess.reveal(c, PUBLIC).value();
sess.finalize();
```

Because the body is written over a generic context, the **same** circuit
function (e.g. `[](auto a, auto b){ return a + b; }`) compiled once with
`frontend::compile` runs unchanged on `ClearCtx` (plaintext) and `SH2PCCtx`
(garbled). **This is the point** — write once, run on whichever context the
caller wants. `sess.num_and()` reports the ANDs garbled so far.

`NetIO` satisfies the `IOChannel` contract; it is not thread-safe — see
[docs/io_channel.md](io_channel.md).

For malicious-secure 2PC use **emp-ag2pc**; for malicious-secure
multi-party (n ≥ 3) use **emp-agmpc** (in `ref/`).

---

## 7. What does not translate

Refuse, or flag for the user, when the source program does any of these:

1. **Secret-dependent control flow** that has no public worst-case
   bound (unbounded `while`, recursion on a secret, secret-driven goto).
2. **Random access into a structure whose size depends on a secret.**
   Linear-scan over a public upper bound is the only escape, and it
   may be prohibitively expensive — flag the cost.
3. **Dynamic memory whose lifetime depends on a secret** (e.g. `new`
   inside a secret-conditional branch).
4. **I/O, syscalls, RNG** inside the circuit body. Randomness must be
   sampled outside (each party samples its own), or bound publicly via
   shared seeds, before circuit construction.
5. **Exceptions / longjmp / Python `try/except`** as data-dependent
   control flow. Must be flattened to `Bit`-tagged "result-or-error"
   pairs.
6. **Pointer comparisons, address-of operations, anything depending on
   the host memory layout.** Only *values* are circuit-representable.
7. **Foreign function calls** to host code that isn't itself
   translated. The circuit is closed; you can't `call printf()` from
   inside.
8. **Wide / arbitrary-precision integers** beyond what the source
   actually uses. Python `int` is unbounded; you must commit to an N
   (the integer value types carry their clear value in a 64-bit codec, so
   `N ≤ 64`). GMP / `mpz_t` operations have no direct EMP equivalent —
   they need a custom multi-limb construction.
9. **Float width**. emp-tool ships `Float_T<Ctx,16>` / `<Ctx,32>` /
   `<Ctx,64>` (IEEE binary16 / binary32 / binary64), all correctly-rounded.
   Map `float` to `Float_T<Ctx,32>` and `double` to `Float_T<Ctx,64>`; pick
   width 16 only when the source is explicitly half-precision.
   Transcendentals (`sin`/`cos`/`exp`/`log`/…) are not provided —
   fixed-point or a polynomial approximation if the source needs them.
10. **String operations whose result length depends on the input
    bytes** (`strlen` on secret data, `split`, regex). Translate only
    fixed-shape byte transformations; pad inputs and outputs to public
    maxima.

---

## 8. Translation checklist

Before declaring a translation done, verify each of:

- [ ] Every input is fed via the session's `input<T>(owner, …)` with an
      owning party (`ALICE`, `BOB`, or `PUBLIC`).
- [ ] Every output is `reveal`'d (through the session) to the intended
      recipient — and **no one else**.
- [ ] No host-language `if` / `while` / `for` / ternary branches on a
      value derived from a secret input. (Greppable: every `if` and
      `while` in the translated source is on a public counter or a
      revealed value.)
- [ ] Every loop bound is a public constant or a runtime-public int.
- [ ] Every array index that varies with a secret is implemented as a
      linear `select`-mux.
- [ ] All operands of every `+ - * / % & | ^ < <= > >= == !=` have
      equal width N (it is part of the type).
- [ ] `Int_T` and `UInt_T` are not silently mixed; pick the family that
      matches the source's signedness.
- [ ] No `reveal` inside the hot loop unless the leak is explicitly
      part of the protocol design.
- [ ] Run on `ClearCtx` and confirm output matches the original program
      on a representative input set.
- [ ] Count ANDs (via `CountCtx` or a recorded program) and confirm the
      gate count is sane (orders of magnitude: ~31 ANDs per 32-bit add,
      ~1000 per 32-bit mul, ~1000 per 32-bit div, ~10⁴–10⁵ per Float op).

---

## 9. Worked example: secret comparison + conditional sum

**Source (Python):**

```python
def f(alice_xs, bob_y):
    total = 0
    for x in alice_xs:                  # alice_xs: list of int32, len = N (public)
        if x > bob_y:
            total += x
    return total                        # revealed to both
```

**EMP translation (C++), over the garbled `SH2PCSession`:**

```cpp
#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/typed.h"
#include "emp-sh2pc/emp-sh2pc.h"
using namespace emp;

constexpr size_t N = 16;                       // public
using Ctx = SH2PCSession::ctx_t;
using S32 = Int_T<Ctx, 32>;                    // x and y are int32

int32_t f(SH2PCSession& sess, const int32_t alice_xs[N], int32_t bob_y_value) {
    auto y     = sess.input<S32>(BOB, bob_y_value);
    auto total = S32::constant(sess.ctx(), 0);
    auto zero  = S32::constant(sess.ctx(), 0);

    for (size_t i = 0; i < N; ++i) {
        // Each party feeds its own value; the other passes a dummy.
        auto x      = sess.input<S32>(ALICE, alice_xs[i]);
        auto gt     = x > y;                     // signed comparison -> Bit_T
        auto addend = zero.select(gt, x);        // gt ? x : 0
        total = total + addend;
    }
    return (int32_t)sess.reveal(total, PUBLIC).value();
}

int main(int argc, char** argv) {
    int party = std::atoi(argv[1]);
    NetIO io(party == ALICE ? nullptr : "127.0.0.1", 12345);
    SH2PCSession sess(&io, party);

    int32_t alice_xs[N] = { /* Alice's real values; Bob's process passes 0s here */ };
    int32_t bob_y       = /* Bob's real value;       Alice's process passes 0   */;

    int32_t r = f(sess, alice_xs, bob_y);
    sess.finalize();
    std::cout << r << "\n";
}
```

Notes on what changed:

* `if x > bob_y: total += x` became `zero.select(x > y, x)` (i.e.
  `(x > y) ? x : 0`) then an unconditional `+`. Both branches happen for
  every `i`.
* `total = 0` became `S32::constant(ctx, 0)`. The accumulator is a circuit
  value, not an `int`, after the first iteration.
* The function returns an `int32_t` only because `sess.reveal(total, PUBLIC)`
  was called at the end. All intermediate state is `S32`.
* `for x in alice_xs` works because `N` is public and known at
  translation time.
* To test in plaintext first, write the loop body over a generic `Ctx` (or
  `compile` it once) and run it on `ClearCtx` before standing up two parties.

---

## 10. Quick reference: API surface

The value types are over a `BooleanContext` `Ctx`. Make a public constant with
`T::constant(sess.ctx(), v)`; feed a secret through the session with
`sess.input<T>(owner, v)`; open with `sess.reveal(v, recipient)` — a
`std::optional<clear_t>`, populated only on a party that learns the value.

```
Bit_T<Ctx>                                 // wire-level boolean (clear: bool)
  & | ^ ! == != -> Bit_T
  select(sel, t) -> Bit_T                  // sel ? t : *this
  constant(ctx, bool)

UInt_T<Ctx,N>                              // unsigned N-bit (clear: uint64_t, N<=64)
  + - * / %                                // wraps mod 2^N; div/mod by 0 saturates
  & | ^ ~                                  // bitwise
  < <= > >= == != -> Bit_T                 // unsigned
  select(sel, t) -> UInt_T                 // sel ? t : *this
  << >> by public int (free), rotl/rotr    // logical, zero-fill
  << >> by UInt_T (secret amount)          // barrel shifter, costs ANDs
  slice<Lo,Hi>() extract<B,W>() concat(hi) // compile-time width changes (free)
  zext<M>() trunc<M>()
  popcount<R>() hamming_weight() leading_zeros() mod_exp(p, q)
  as_signed()                              // reinterpret wires (free)
  operator[i] -> Bit_T (i in [0,N))
  constant(ctx, uint64_t)

Int_T<Ctx,N>                               // signed N-bit (clear: int64_t, N<=64)
  + - * / %                                // wraps mod 2^N (matches int{N}_t HW)
  - (unary)                                // two's-complement negate (no .abs)
  & | ^ ~                                  // bitwise
  < <= > >= == != -> Bit_T                 // signed
  select(sel, t) -> Int_T                  // sel ? t : *this
  << >> by public int (free)               // >> is arithmetic (sign-fill)
  << >> by UInt_T<Ctx,N> (secret amount)   // barrel shifter, costs ANDs
  sext<M>() trunc<M>()                     // compile-time width changes
  as_unsigned()                            // reinterpret wires (free)
  operator[i] -> Bit_T (i in [0,N))
  constant(ctx, int64_t)

Float_T<Ctx,W>                             // IEEE binary{16,32,64} (clear: host float)
  + - * / unary-
  sqr sqrt recip rsqrt fma min max
  equal/not_equal less_than/less_equal/greater_than/greater_equal -> Bit_T
  also operators < <= > >= == != -> Bit_T  // NaN-aware
  is_nan(), is_inf(), is_zero() -> Bit_T
  abs(), copysign(rhs), select(sel, o)
  operator[i] -> Bit_T (i in [0,W))
  constant(ctx, host_float) / from_bits(ctx, bits)

BitVec_T<Ctx,N>                            // fixed bit-vector (clear: std::array<bool,N>)
  & | ^ ~
  == != -> Bit_T
  select(sel, t) -> BitVec_T
  << >> by public int                       // logical shifts
  slice<Lo,Hi>(), concat(rhs), as_uint()
  operator[i] -> Bit_T (i in [0,N))
```

Constants in `emp::`: `PUBLIC = 0`, `ALICE = 1`, `BOB = 2`. Party arguments
are plain `int`; widths are template parameters.

The context is passed explicitly — there is no global backend.
`ClearCtx` (plaintext), `RecordCtx` (records a `BooleanProgram`), `CountCtx` /
`DigestCtx` (gate-count / determinism analysis), and emp-sh2pc's `SH2PCCtx`
(garbled 2PC) are all `BooleanContext`s; the same typed body runs on each.

The crypto circuits live in `emp::circuit::crypto` (`circuits/crypto/aes128.h` /
`sha256.h` / `keccak.h`) as context-generic kernels with `BitVec_T` message/block
interfaces; the `fp<W>_*` float suite and
the fixed-width AES/SHA assets also ship as native `.empbc` programs you can load
and replay on your context (see [frontend.md](frontend.md) and the `.empbc` section
of the README).

---

## 11. Where to read more

* `emp-tool/circuits/typed.h` — umbrella for the context-bound value types
  (`Bit_T`/`BitVec_T`/`UInt_T`/`Int_T`/`Float_T<Ctx>`); the per-type headers it
  pulls in are the source of truth for the op set, and the `emp::kernel`
  arithmetic kernels they build on live in `emp-tool/circuits/numeric_kernels.h`.
* `emp-tool/ir/context/context.h` — the `BooleanContext` concept and the
  contexts (`ClearCtx`, `RecordCtx`, `CountCtx`, `DigestCtx`), plus
  `execute_program(ctx, prog, inputs)` for replaying a loaded program.
* `emp-tool/circuits/frontend/circuit_fn.h` + `circuits/frontend/rec.h` — `compile` / `run`
  for pure circuit functions; see [frontend.md](frontend.md).
* `emp-sh2pc/emp-sh2pc/sh2pc_session.h` — `SH2PCSession`, the garbled 2PC
  session: typed `input` / `reveal` over its `SH2PCCtx` gate context (`sess.ctx()`).
* `test/circuits/test_typed.cpp` — tutorial for the context-bound value types over
  `ClearCtx`.
* `test/circuits/test_circuit_fn.cpp` — compile-once / run-on-any-context, both body
  forms, a nullary circuit, the 31-AND adder, deterministic recording.
* `emp-sh2pc/test/test_circuit_fn_sh2pc.cpp` — the same compiled circuit run
  two-party over `SH2PCCtx`.
* `test/ir/test_builtin_circuits.cpp` — the prebuilt `.empbc` crypto builtins
  replayed through `ClearCtx` and checked against the native kernels.
* `test/circuits/test_crypto_{aes,sha256,keccak}.cpp` — the BooleanContext-native AES /
  SHA-256 / Keccak circuits vs known vectors, record/replay, and gate counts.
* [circuits.md](circuits.md) — the circuit value layer conventions;
  [numeric_semantics.md](numeric_semantics.md) for wrap/division/comparison details.
