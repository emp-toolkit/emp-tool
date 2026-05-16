# Translating C++ / Python to EMP secure circuits

A reference for AI agents asked to take ordinary code and produce a
boolean-circuit equivalent that runs on **emp-tool**'s circuit frontend
(`Bit_T` / `BitVec_T` / `UnsignedInt_T` / `SignedInt_T` / `Float_T`)
backed by a `Backend` from `emp-tool/execution/`.

If you read only one section, read **§1 The hard rule** and **§4
Translation patterns**. The rest is type detail.

---

## 0. Mental model in three sentences

EMP evaluates a fixed boolean circuit (AND / XOR / NOT gates over wires)
once. Each input wire is owned by exactly one party — `ALICE`, `BOB`, or
`PUBLIC` (everyone agrees) — and each output wire is revealed to a
chosen party. The same source code runs on every backend
(`ClearBackend` for plaintext / circuit dumping, `HalfGateGen` /
`HalfGateEva` for garbled 2PC, plus the protocol-driver layers in
`emp-sh2pc` / `emp-ag2pc` / `emp-agmpc` that wrap input OT around the
gate engine); nothing about the circuit changes per backend.

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
(`.select(sel, rhs)` / `If(sel).Then(a).Else(b)`). Loops must have public bounds.
Arrays at secret indices must be linearly enumerated. See §4.

If your input C++ / Python relies on early exit or data-dependent loop
termination, **stop and either find a public worst-case bound, or
report that the program cannot be translated as-written**.

---

## 2. Type mapping

| Source type                       | EMP type                              | Notes |
|-----------------------------------|----------------------------------------|---|
| `bool`                            | `Bit`                                  | `Bit(value, party)` |
| `uint8_t … uint64_t`, `unsigned`  | `UnsignedInt(W, value, party)`         | W = bit width, must match exactly |
| `int8_t … int64_t`, `int`         | `SignedInt(W, value, party)`           | two's complement |
| `__uint128_t`, `__int128`         | `UnsignedInt(128, …)` / `SignedInt(128, …)` | constructor accepts 128-bit literals |
| `float` (IEEE 754 binary32)       | `Float`                                | only float32; no float64 / double |
| `double`                          | **not supported** — re-target to `Float`, or run in fixed-point with `SignedInt` |
| fixed-size bit array / packed flags | `BitVec(W, value, party)`            | no arithmetic, only `& \| ^ ~ << >>` and slice / concat |
| Python `int` (arbitrary precision)| `SignedInt(W, …)` after **you** pick W | translator must commit to a width |
| Python `bool`                     | `Bit`                                  | |
| Python `float`                    | `Float`                                | |
| `std::string`, `bytes`, `bytearray` | array of `BitVec(8, …)` of fixed length | length is public; pad to max |
| `std::vector<T>` (fixed size)     | `std::vector<EMPType>` of public length | |
| `std::vector<T>` (secret size)    | **not supported** — pad to public max length and carry a `valid` `Bit` per slot |
| `std::map`, `std::unordered_map`  | **not supported** — flatten to parallel arrays + linear scan |
| `std::optional<T>`                | `pair<T, Bit_valid>`                   | |
| pointers / references to host data| not represented; only the *value* matters |

The class definitions in `emp-tool/circuits/*.h` are templated on a
`Wire` type; the aliases in `emp-tool/emp-tool.h` (`Bit`, `BitVec`,
`UnsignedInt`, `SignedInt`, `Float`) all instantiate at `block` (the
default 128-bit garbled-circuit wire). Use the aliases. Only reach
for `Bit_T<MyWire>` etc. if you are also writing a custom `Backend`.

### 2.1. Width is mandatory

`SignedInt(value, ...)` does **not** exist. Every integer needs an
explicit bit width:

```cpp
SignedInt   a(32, -7, ALICE);     // not SignedInt(-7, ALICE)
UnsignedInt b(64, 0xDEADBEEFull, BOB);
```

When translating, pick W from the source type:

* `int32_t` → 32, `int64_t` → 64, `int` → 32 (assuming LP64), `size_t`
  → 64 on 64-bit hosts, etc.
* Python `int`: pick the smallest power-of-two width that holds the
  range used in the program. If unclear, default to 64; document the
  choice in a comment.

### 2.2. Mixing widths

Operands of `+ - * / % & | ^ < <= > >= == !=` **must have equal
width**. If they differ, use `.resize(W)` *first*:

```cpp
SignedInt a(32, x, ALICE);
SignedInt b(64, y, BOB);
SignedInt sum = a.resize(64) + b;            // both 64-bit
```

`resize` on `SignedInt` sign-extends; on `UnsignedInt` it zero-extends;
truncation drops high bits. Resize is structural — costs no AND gates.

### 2.3. Mixing signed and unsigned

`SignedInt` and `UnsignedInt` are different types. To convert, use the
explicit bit-cast:

```cpp
UnsignedInt u = s.as_unsigned();   // 0 gates, just a type tag flip
SignedInt   s = u.as_signed();
```

Width is preserved. Bits are reinterpreted exactly as in C
(`(uint32_t)int32_x`, `(int32_t)uint32_x`).

---

## 3. Inputs, outputs, parties

### 3.1. Constructing input wires

```cpp
SignedInt   a(32, alice_value, ALICE);   // owned by Alice; Bob sees garbage at run time
UnsignedInt b(32, bob_value,   BOB);     // owned by Bob
SignedInt   k(32, 17,          PUBLIC);  // both parties agree on the literal
```

The `value` argument on a non-`PUBLIC` party is **only read on that
party's process**. The other party's program passes a dummy of the
correct type; the protocol routes the real bits through OT.

### 3.2. Revealing outputs

```cpp
int32_t  r = result.reveal<int32_t>(PUBLIC);   // both parties see the int
uint64_t r = result.reveal<uint64_t>(ALICE);   // only Alice
bool     b = predicate.reveal<bool>(PUBLIC);
std::string bits = bv.reveal<std::string>(PUBLIC);  // MSB-first bit string
```

`reveal` is the *only* way to get a host-language value back from a
circuit value. Calling `reveal<bool>(PUBLIC)` on a secret comparison
**leaks that bit to both parties**. This is sometimes intentional and
sometimes a security bug — the translator must not insert reveals it
wasn't asked for.

### 3.3. Single binary

In emp-tool the same source file is compiled once and run as both
parties (e.g. `./prog 1 12345` for Alice, `./prog 2 12345` for Bob).
The party identity is a runtime int, typically read from `argv[1]`.
Both invocations pass *both* `ALICE` and `BOB` constructors — each
process supplies its own real value where it owns the input and a
placeholder elsewhere. Translators should preserve this symmetry.

---

## 4. Translation patterns

### 4.1. `if / else` on a secret value

```cpp
// C++ source:
int max = (a > b) ? a : b;

// EMP:
SignedInt max = If(a > b).Then(a).Else(b);     // fluent builder from sortable.h
// or, equivalently, the underlying method form
// (b.select(sel, rhs) returns sel ? rhs : b):
SignedInt max = b.select(a > b, a);
```

`a > b` returns `Bit`. `If(cond).Then(x).Else(y)` returns `x` when
`cond` is true, `y` otherwise. Both branches are always evaluated.

### 4.2. `if (cond) x = y;` (mutation form)

```cpp
// Source:
if (cond) x = y;

// EMP:
x = If(cond_bit).Then(y).Else(x);    // identity on the false side
```

### 4.3. Comparisons

`< <= > >= == !=` on `UnsignedInt`, `SignedInt`, `Float`, and `BitVec`
return a `Bit_T<Wire>`. They do **not** return host `bool`. You can
only use one in host-language `if`/`while` after a `reveal` — which
leaks. Keep comparisons inside the circuit by feeding the resulting
`Bit` into `If`, `select`, `&`, `|`, `!`.

`UnsignedInt` comparisons are unsigned; `SignedInt` comparisons are
signed (sign-extend by 1, subtract, look at the top bit).

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
Bit running(true, PUBLIC);
for (size_t i = 0; i < MAX; ++i) {
    Bit alive = running & (n > UnsignedInt(W, 0, PUBLIC));
    UnsignedInt next_acc = acc + step(n);
    acc = If(alive).Then(next_acc).Else(acc);
    n   = If(alive).Then(n - UnsignedInt(W, 1, PUBLIC)).Else(n);
    running = alive;
}
```

If MAX is unknown, the program is not translatable.

### 4.5. Array read at a secret index

```cpp
// Source:
//   y = arr[idx];   // idx secret, arr public-length

// EMP: linear mux. Cost is O(len(arr)) in AND gates.
SignedInt y(W, 0, PUBLIC);
for (size_t k = 0; k < arr.size(); ++k) {
    Bit eq = (idx == UnsignedInt(IDX_W, k, PUBLIC));
    y = If(eq).Then(arr[k]).Else(y);
}
```

### 4.6. Array write at a secret index

```cpp
// arr[idx] = v;
for (size_t k = 0; k < arr.size(); ++k) {
    Bit eq = (idx == UnsignedInt(IDX_W, k, PUBLIC));
    arr[k] = If(eq).Then(v).Else(arr[k]);
}
```

Cost: O(len(arr)) per write. If you have many writes, restructure to
use one of the ORAM constructions in the wider literature; emp-tool
itself ships no ORAM.

### 4.7. Min / max over an array

```cpp
SignedInt best = a[0];
for (size_t i = 1; i < a.size(); ++i)
    best = If(a[i] < best).Then(a[i]).Else(best);
```

### 4.8. Counting matches (popcount-style)

```cpp
UnsignedInt count(W, 0u, PUBLIC);
for (size_t i = 0; i < a.size(); ++i)
    count = count + If(a[i] == target).Then(UnsignedInt(W, 1u, PUBLIC))
                                      .Else(UnsignedInt(W, 0u, PUBLIC));
// or, more idiomatic when N fits in one BitVec: assemble matches into a
// width-N BitVec, view it as UnsignedInt, and call `.hamming_weight()`.
```

### 4.9. Bit-level packing / unpacking

`BitVec` supplies `slice(lo, hi)` for `[lo, hi)` and `concat(hi)` for
`lo | (hi << this->size())`:

```cpp
BitVec all = lo.concat(hi);          // size = lo.size() + hi.size()
BitVec byte0 = word.slice(0, 8);     // low byte
```

Index `0` is the LSB everywhere in emp-tool. `bv.reveal<std::string>()`
returns MSB-first.

### 4.10. Sorting (oblivious)

`emp-tool/circuits/sortable.h` ships a Batcher bitonic sort that works
for any class implementing `geq` / `equal` / `select` — which means
`UnsignedInt`, `SignedInt`, `Float`, and `Bit` all qualify:

```cpp
UnsignedInt arr[N];
// ... fill ...
sort(arr, N);                        // ascending; pass Bit(false, PUBLIC) for descending
```

Cost is O(N log² N) compare-swaps; each compare-swap is O(W) gates.

### 4.11. Floats

`Float` is IEEE 754 binary32. It supplies `+ - * / unary-`, `sqr`,
`sqrt`, `sin`, `cos`, `exp`, `exp2`, `ln`, `log2`, plus `equal`,
`less_than`, `less_equal`, and `select`. `==` and `!=` come from the
Sortable mixin (which dispatches to `equal`); `<`, `<=`, `>`, `>=`
aren't wired up — call `less_than` / `less_equal` directly (and
`!less_than` for `>`). There is no float64.

Float circuits are large (thousands of AND gates for a single mul).
For ML-style fixed-point work, prefer `SignedInt` with a chosen scale.

### 4.12. Crypto primitives (AES-128, SHA3-256)

Don't hand-roll AES or SHA3 in `Bit`/`BitVec` ops. emp-tool ships
pre-built Bristol circuits wrapped in calculator classes:

```cpp
// SHA3-256 over a secret byte string of public length 2000.
BitVec input[2000];        // each entry is BitVec(8, ..., ALICE/BOB/PUBLIC)
BitVec output(10, (uint32_t)32, PUBLIC);   // 32 = output length in bytes
SHA3_256_Calculator sha3;
sha3.sha3_256(&output, input, /*input_count=*/2000);
uint8_t hash[32];
output.reveal(hash, PUBLIC);                // raw byte reveal

// AES-128-CTR with a secret key over secret data, public nonce / length.
// (See test/test_aes_128_ctr.cpp — it walks both the in-circuit and the
// reference OpenSSL paths, useful as a template.)
AES_128_CTR_Calculator aes;
// ... see header for full signature ...
```

The Calculator classes are wrappers around Bristol-format AES-128 and
Keccak-f circuits embedded into the binary at compile time. Cost is
≈6800 ANDs for one AES block, ≈37k for one Keccak-f permutation.

Because both circuits *are* circuits, every byte of input is an
emp-tool wire (`BitVec(8, byte, party)`), every byte of output is a
wire that you reveal to a chosen party. There are no host-side
crypto branches inside.

`emp::aes_128_ctr(...)` and `emp::sha3_256(...)` (free functions, no
`_Calculator` suffix) are **non-circuit** OpenSSL-backed helpers for
ground-truth comparisons in tests. Use those to verify the calculator
output, not as part of the circuit.

---

## 5. Semantics that bite the translator

These match **hardware** two's-complement semantics, not C standard
"undefined behavior". A naive translator that reproduces the source's
UB-avoidance dance will produce extra gates for no reason.

* **Signed wrap**: `+`, `-`, `*` on `SignedInt` wrap mod 2^W. This
  matches `int{N}_t` on x86 / arm. C calls signed overflow UB; emp-tool
  is deterministic. Don't insert `__builtin_add_overflow` checks.
* **`INT_MIN / -1`**: returns `INT_MIN` (the bit pattern 2's-complement
  division produces). C calls this UB. Don't guard.
* **Division by zero**: undefined behavior of the *circuit*. The
  caller must ensure the divisor is nonzero. If the divisor is secret,
  guard with an `If` that picks a sentinel result and a "valid" `Bit`.
* **Signed `%`**: sign of result follows the dividend (C99+).
* **Shift amount ≥ width**: logical shifts produce 0; `SignedInt`
  arithmetic right shift produces `0` for non-negative operands or
  `-1` for negative ones (sign-fill). Both static-shamt and
  dynamic-shamt forms exist; the dynamic form treats `shamt` as
  unsigned.
* **`>>` polarity**: logical on `BitVec` and `UnsignedInt`, arithmetic
  on `SignedInt`. To do logical right-shift on a `SignedInt`:
  `s.as_unsigned() >> k` (then `.as_signed()` if you need the type
  back).
* **`<<`**: always logical.
* **`abs(INT_MIN)`**: returns `INT_MIN` as a bit pattern (the
  unrepresentable case). The result type is `UnsignedInt`, where this
  bit pattern is `2^(W-1)` and is faithful as a *magnitude*. No
  exception.
* **`==` / `!=` cost**: equal-test is cheap on `Bit` (one XOR), grows
  to a tree of XORs+ANDs on multi-bit types. `<` etc. are subtraction
  followed by a sign check, which is the dominant cost. Avoid
  redundant comparisons.
* **`-x` on `UnsignedInt`**: well-defined as `~x + 1` mod 2^W
  (i.e. `0u - x`). Matches C unsigned negation. The result is still
  `UnsignedInt`.

---

## 6. Backend setup boilerplate

### 6.1. Plaintext / circuit-dump (for testing translations)

```cpp
#include "emp-tool/emp-tool.h"
using namespace emp;

int main() {
    setup_clear_backend();              // optionally: setup_clear_backend("circuit.txt");
    // ... circuit code ...
    finalize_clear_backend();
}
```

`ClearBackend` evaluates the circuit in cleartext (it sees both
parties' "secret" inputs because there's only one process). Use it to
verify a translation produces the same output as the original C++ /
Python before you stand up two real parties. Pass a filename to also
dump a Bristol-format circuit file on `finalize`.

`backend->num_and()` returns the AND-gate count after a run — the
right metric for circuit cost. (XOR / NOT are free in modern garbling
schemes.)

### 6.2. Real semi-honest 2PC

The bare `HalfGate` / `PrivacyFree` classes in emp-tool don't run a
full protocol on their own (no OT for input wires); they're the
gate-evaluation engine that the protocol layers above plug in. For
end-to-end semi-honest 2PC use **emp-sh2pc**:

```cpp
NetIO io(party == ALICE ? nullptr : "127.0.0.1", port);
setup_semi_honest(&io, party);          // from emp-sh2pc
// ... circuit code, identical to the ClearBackend version ...
finalize_semi_honest();
```

The circuit body is byte-for-byte the same as under `ClearBackend`.
**This is the point** — translate once, run on whichever backend the
caller wants.

`NetIO` (`"wb"` stdio + raw read on recv) satisfies the `IOChannel`
contract and can be passed to any of the `setup_*` helpers. Not
thread-safe — see [docs/io_channel.md](io_channel.md).

For malicious-secure 2PC use **emp-ag2pc**; for malicious-secure
multi-party (n ≥ 3) use **emp-agmpc** (in `ref/`).

### 6.3. Direct HalfGate (custom protocols)

If you're writing your own 2PC protocol shell rather than using
emp-sh2pc / emp-ag2pc, instantiate the gate engines directly:

```cpp
NetIO io(party == ALICE ? nullptr : "127.0.0.1", port);
backend = (party == ALICE) ? (Backend*)new HalfGateGen(&io)
                           : (Backend*)new HalfGateEva(&io);
// ... circuit code ...
delete backend; backend = nullptr;
```

You're now responsible for the OT-driven `feed` / `reveal` surrounding
the circuit. `test/test_garble.cpp` is the closest in-tree example. Most
users do **not** want this path — the higher-level libraries above
already handle it.

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
   actually uses. Python `int` is unbounded; you must commit to a W.
   GMP / `mpz_t` operations have no direct EMP equivalent — they need
   a custom multi-limb construction.
9. **`double`-precision float**. emp-tool ships only `float32`. If the
   source genuinely needs `double`, either reduce to `float` (with the
   user's blessing — accuracy loss) or refuse.
10. **String operations whose result length depends on the input
    bytes** (`strlen` on secret data, `split`, regex). Translate only
    fixed-shape byte transformations; pad inputs and outputs to public
    maxima.

---

## 8. Translation checklist

Before declaring a translation done, verify each of:

- [ ] Every input has an owning party (`ALICE`, `BOB`, or `PUBLIC`).
- [ ] Every output is `reveal`'d to the intended recipient — and **no
      one else**.
- [ ] No host-language `if` / `while` / `for` / ternary branches on a
      value derived from a secret input. (Greppable: every `if` and
      `while` in the translated source is on a public counter or a
      revealed value.)
- [ ] Every loop bound is a public constant or a runtime-public int.
- [ ] Every array index that varies with a secret is implemented as a
      linear `If`-mux.
- [ ] All operands of every `+ - * / % & | ^ < <= > >= == !=` have
      equal width. (Use `.resize(W)` to match.)
- [ ] `SignedInt` and `UnsignedInt` are not silently mixed; conversions
      are explicit `.as_signed()` / `.as_unsigned()`.
- [ ] No `reveal` inside the hot loop unless the leak is explicitly
      part of the protocol design.
- [ ] Run under `setup_clear_backend()` and confirm output matches the
      original program on a representative input set.
- [ ] Print `backend->num_and()` and confirm the gate count is sane
      (orders of magnitude: ~64 ANDs per 32-bit add, ~1500 per 32-bit
      mul, ~5000 per 32-bit div, ~10⁴–10⁵ per Float op).

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

**EMP translation (C++):**

```cpp
#include "emp-tool/emp-tool.h"
using namespace emp;

constexpr size_t N = 16;                       // public
constexpr int W = 32;                          // x and y are int32

int32_t f(const int32_t alice_xs[N], int32_t bob_y_value, int party) {
    SignedInt y(W, bob_y_value, BOB);
    SignedInt total(W, 0, PUBLIC);

    for (size_t i = 0; i < N; ++i) {
        // Each party feeds its own value; the other passes a dummy.
        SignedInt x(W, alice_xs[i], ALICE);
        Bit       gt   = (x > y);              // signed comparison, returns Bit
        SignedInt addend = If(gt).Then(x).Else(SignedInt(W, 0, PUBLIC));
        total = total + addend;
    }
    return total.reveal<int32_t>(PUBLIC);
}

int main(int argc, char** argv) {
    int party = std::atoi(argv[1]);
    NetIO io(party == ALICE ? nullptr : "127.0.0.1", 12345);
    // For semi-honest 2PC, swap in setup_semi_honest(&io, party); from emp-sh2pc.
    setup_clear_backend();                     // testing only

    int32_t alice_xs[N] = { /* Alice's real values; Bob's process passes 0s here */ };
    int32_t bob_y       = /* Bob's real value;       Alice's process passes 0   */;

    int32_t r = f(alice_xs, bob_y, party);
    std::cout << r << "\n";
    finalize_clear_backend();
}
```

Notes on what changed:

* `if x > bob_y: total += x` became `If(x > y).Then(x).Else(0)` then unconditional
  `+`. Both branches happen for every `i`.
* `total = 0` became `SignedInt(W, 0, PUBLIC)`. The accumulator is a
  circuit value, not an `int`, after the first iteration.
* The function returns an `int32_t` only because `reveal<int32_t>` was
  called at the end. All intermediate state is `SignedInt`.
* `for x in alice_xs` works because `N` is public and known at
  translation time.

---

## 10. Quick reference: API surface

```
Bit(bool, party)                           // wire-level boolean
  & | ^ ! != ==  -> Bit
  select(sel, b) -> Bit
  reveal<bool|string>(party)

BitVec(W, value, party)                    // raw W-bit vector, no arithmetic
  & | ^ ~                                  // equal-width
  << >> (size_t)                           // logical shifts
  equal(rhs) -> Bit
  concat(hi) -> wider BitVec
  slice(lo, hi) -> sub BitVec
  select(sel_bit, rhs) -> BitVec
  operator[i] -> Bit&
  reveal<integral|string>(party)
  reveal(void* out, party)

UnsignedInt(W, value, party) : BitVec
  + - * / %                                // wraps mod 2^W
  - (unary)                                // = 0 - x mod 2^W
  < <= > >= == != -> Bit                   // unsigned
  & | ^ ~                                  // typed UnsignedInt
  << >> (size_t / UnsignedInt)             // both logical
  resize(W')                               // zero-extend / truncate
  as_signed() -> SignedInt                 // bit-cast, no gates
  leading_zeros(), hamming_weight()        // -> UnsignedInt
  mod_exp(p, q) -> UnsignedInt
  reveal<unsigned-integral|string>(party)

SignedInt(W, value, party) : BitVec
  + - * / %                                // wraps mod 2^W (matches int{W}_t HW)
  - (unary)                                // two's-complement negate
  < <= > >= == != -> Bit                   // signed
  & | ^ ~                                  // typed SignedInt
  << (size_t / UnsignedInt)                // logical
  >> (size_t / UnsignedInt)                // arithmetic (sign-fill)
  resize(W')                               // sign-extend / truncate
  abs() -> UnsignedInt                     // abs(MIN) wraps to MIN bit pattern
  as_unsigned() -> UnsignedInt             // bit-cast, no gates
  reveal<signed-integral|string>(party)

Float(float, party)                        // IEEE 754 binary32
  + - * / unary-
  sqr sqrt sin cos exp exp2 ln log2
  equal(rhs) less_than(rhs) less_equal(rhs) -> Bit
  abs(), select(sel, rhs)
  operator[i] -> Bit& (i in [0, 32))
  reveal<float|double|string>(party)

If(sel_bit).Then(x).Else(y) -> T           // fluent builder from sortable.h
sort(arr, n, data=nullptr, acc=true)       // bitonic, in-place

setup_clear_backend([filename])            // ClearBackend; optional Bristol dump
finalize_clear_backend()
backend->num_and()                         // AND-gate count

// Pre-built crypto circuits (emp-tool/circuits/{aes_128_ctr,sha3_256}.h):
SHA3_256_Calculator                         // Keccak-f-based SHA3-256
  sha3_256(BitVec* out, BitVec* in, count)  //   wires-in, wires-out
AES_128_CTR_Calculator                      // AES-128 in CTR mode
  // see header for full signature

// Non-circuit reference (OpenSSL-backed; for tests / ground truth):
emp::sha3_256(uint8_t* out, const T* in, size_t len)
emp::aes_128_ctr(__m128i key, __m128i iv, T* in, uint8_t* out, len, chunk)
```

Constants in `emp::`: `PUBLIC = 0`, `ALICE = 1`, `BOB = 2`. Width and
party arguments are plain `int` / `size_t`; no enum.

`backend` is the global `Backend*` (thread-local under
`EMP_TOOL_THREADING`) that every circuit op dispatches through. The
`setup_*` helpers (`setup_clear_backend`, `setup_semi_honest`, ...)
construct one and assign it; the matching `finalize_*` deletes it.
Don't reference `backend` from circuit code — use the wrapper types.

---

## 11. Where to read more

* `emp-tool/execution/backend.h` — the `Backend` interface every
  protocol implements (`feed`, `reveal`, `and_gate`, `xor_gate`,
  `not_gate`, `public_label`, plus bulk variants).
* `emp-tool/execution/clear_backend.h` — plaintext / circuit-printer
  reference backend. Read the comments at the top — they spell out the
  free / non-free gate convention.
* `emp-tool/execution/half_gate.h`, `privacy_free.h` — garbling
  backends. `HalfGateGen` (Alice-side) and `HalfGateEva` (Bob-side)
  are the concrete classes; `HalfGate` is the protocol-agnostic base.
  Same shape for `PrivacyFreeGen` / `PrivacyFreeEva`.
* `emp-tool/circuits/{bit,bitvec,unsigned_int,signed_int,float32}.h`
  — the user-facing circuit primitives. The headers are short; read
  them.
* `emp-tool/circuits/sortable.h` — `If(cond).Then(x).Else(y)` and
  `sort(arr, n)`, plus the `Sortable` mixin every numeric type
  inherits from.
* `emp-tool/circuits/{aes_128_ctr,sha3_256}.h` — pre-built crypto
  circuits with calculator classes; also the non-circuit OpenSSL
  reference functions you'd use to verify them.
* `test/test_{bit,bitvec,uint,int,float}.cpp` — each test file's
  `example()` block is a tutorial for the corresponding primitive.
  Particularly `test/test_int.cpp` for `SignedInt`.
* `test/test_{aes_128_ctr,sha3_256}.cpp` — end-to-end examples of using
  the crypto calculators from circuit code, including reveal back to
  host bytes for a ground-truth check.
* `test/test_gen_circuit.cpp` — minimal example of generating a Bristol
  circuit file under `setup_clear_backend("filename")` and converting
  it via `BristolFormat::to_file`.
* `AGENTS.md` (top of repo) — project conventions index, with topical
  subdocs under `docs/` for the circuit-class layer, backend layer,
  numeric semantics, and the IO channel thread-safety contract.
