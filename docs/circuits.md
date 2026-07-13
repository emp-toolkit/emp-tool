# Circuit value layer (`emp-tool/circuits/`)

Conventions for the circuit value types under `emp-tool/circuits/`.

The circuit value layer is the **context-bound typed values**: `Bit_T<Ctx>`,
`UInt_T<Ctx,N>`, `Int_T<Ctx,N>`, `Float_T<Ctx,W>`, and `BitVec_T<Ctx,N>`,
templated on a `BooleanContext` (`ir/context/concept.h`, re-exported by the
`ir/context/context.h` umbrella). Static dispatch, no global backend; each value
carries its own `Ctx*` and issues value-return gates on it.
This is the layer the [frontend](frontend.md) compiles and replays. The
`BooleanContext` is pure circuit execution (gates only); concrete values enter and
leave through a **session** (emp-tool's `ClearSession` in
`ir/session/clear_session.h`, or a protocol library's own session), which owns the
`input`/`reveal` boundary and a direct context. Pure circuit bodies stay
`Ctx`-templated values.

Each value type lives in its own header — `circuits/{bit,bitvec,unsigned_int,
signed_int,float}.h` — over the shared arithmetic in `circuits/numeric_kernels.h`.
Two umbrellas gather them:

- `circuits/typed.h` — the value types (`#include` it to get all five).
- `circuits/circuits.h` — the whole circuits layer: values + `value_traits.h` +
  numeric kernels + sorting (`sort.h`) + the in-circuit crypto
  (`circuits/crypto/crypto.h` = aes128 / sha256 / keccak) + the compile/run frontend.

For numeric semantics (wrap, division, comparison), read
[numeric_semantics.md](numeric_semantics.md). For protocol code that *uses* these
primitives, read [EMP_TRANSLATION.md](EMP_TRANSLATION.md).

## Context-bound values

Each value is a small struct templated on a `BooleanContext` `Ctx`. It holds its
wires inline (`Wire w;` in `Bit_T`, `std::array<Wire,N> w;` in the rest — a
`std::vector<Wire>` for the runtime-width `UInt_T<Ctx,0>` / `Int_T<Ctx,0>`) plus a
private `Ctx*` (reached via `context()`); operators issue value-return gates on the
context. No inheritance, no marker base, no global backend.

Every value provides three contracts that generic code (contexts, the frontend)
relies on:

- **context** — `context() -> Ctx*`, `using context_type = Ctx;`, and
  `template<BooleanContext C2> using rebind = X_T<C2,…>;` ("same value family,
  different context"; compile-time only, moves no data). E.g.
  `UInt_T<RecordCtx,32>::rebind<ClearCtx> == UInt_T<ClearCtx,32>`.
- **wire layout** — `static constexpr int width()`, `void pack_wires(Wire*) const`,
  `static X_T from_wires(Ctx&, const Wire*)`.
- **clear codec** — `static std::array<bool, width()> encode(clear_t)` (LSB-first) and
  `static clear_t decode(const bool*)`, with `using clear_t = …;` (`bool` for
  `Bit_T`, `uint64_t` for `UInt_T`, `int64_t` for `Int_T`, the host float type for
  `Float_T`, `std::array<bool,N>` for `BitVec_T`).

These static members are exposed uniformly through
[`emp::value_traits<T>`](../emp-tool/circuits/value_traits.h)
(`width()`/`encode`/`decode`/`rebind<Ctx>`) — the single metadata accessor over a
value's own static members. A type meeting all three contracts satisfies the
`WireValue` concept (`ir/wire_value.h`); a session can `input`/`reveal` it
and the frontend can `compile`/`run` it.

```cpp
#include "emp-tool/emp-tool.h"
using namespace emp;

ClearSession sess;                          // owns a ClearCtx + the I/O boundary
using Ctx = ClearSession::ctx_t;
using UInt32 = UInt_T<Ctx, 32>;
auto a = sess.input<UInt32>(ALICE, 7);      // feed inputs through the session
auto b = sess.input<UInt32>(BOB,   5);
auto s = a + b;                             // pure value-return gates on the values
auto lt = a < b;                            // -> Bit_T<ClearCtx>
uint32_t r = sess.reveal<uint32_t>(s, PUBLIC).value();  // reveal -> std::optional<uint32_t>
```

A session exposes `input` (and `input_batch` where applicable), `reveal`, and
`ctx()` for value/context-level work such as public constants
(`UInt32::constant(sess.ctx(), 7)`). It names no value family — values are
context-bound (`UInt_T<ctx_t,N>` etc.), so adding a value family needs no
session edit. A protocol library provides its own session over a garbled /
secret-shared context with the same surface — only the constructor (IO, party,
preprocessing) differs. Pure circuit bodies never call `input`/`reveal`; they take and return
values. `reveal` returns `std::optional<clear_t>`: the value on a party that learns
it (every party for `PUBLIC`, the named recipient otherwise) and `std::nullopt` on a
party that does not; a plaintext `ClearSession` always populates it.

### Value surface

- `Bit_T` — `& | ^ ! == !=`, `select`.
- `UInt_T<N>` — `+ - * / %`, comparisons, `& | ^ ~`, public-amount shifts/rotates
  (`<<`/`>>`/`rotl`/`rotr` by `int`), secret-amount shifts (`<<`/`>>` by a
  `UInt_T` — a barrel shifter), `slice`/`extract`/`concat`/`zext`/`trunc`,
  `hamming_weight`/`popcount<R>`, `leading_zeros`, `mod_exp`, `as_signed`.
- `Int_T<N>` — two's-complement `+ - * / %` (truncating), `-`(negate), signed
  comparisons, `& | ^ ~`, logical-left / arithmetic-right shifts, `sext`/`trunc`,
  `as_unsigned`.

**Fixed vs runtime width.** `N > 0` is a fixed-width value; for `N <= 64` it is
a `WireValue` (compile-time width, clear codec, wire layout — the form a session
feeds through `input`/`reveal` and the frontend compiles). A wider fixed-width
`UInt_T` / `Int_T` (`N > 64`) has no 64-bit clear codec, so it is only a
`WireBundle` (frontend-compilable, usable as a circuit argument) but not
session-I/O-eligible — use `BitVec_T<Ctx,N>` for typed session I/O past 64 bits.
`N == 0` (the `runtime_width` sentinel) is
the *same* `UInt_T` / `Int_T` family with the width carried in the wire vector and
chosen at construction — `UInt_T<Ctx,0>(ctx, width)`, `UInt_T<Ctx,0>::constant(ctx,
width, v)`. It shares every operator above through the runtime-sized kernels; the
compile-time-width surface (`slice`/`extract`/`concat`/`zext`/`trunc`, secret-amount
barrel shifts, the clear codec, `popcount<R>`) is `requires (N > 0)` and so absent,
and it adds `resize(width)` plus fixed↔runtime conversion (`to_dynamic()` on a fixed
value, `to_fixed<M>()` on a runtime one). A runtime-width value is **not** a
`WireValue` — it is for data-driven in-circuit computation, not the frontend
`input`/`compile` boundary. (Width must be `>= 1`; wider-than-64 constants
zero-extend for `UInt_T` and sign-extend for `Int_T`.)
- `Float_T<W>` — `+ - * / min max sqrt recip rsqrt fma`, comparisons / `is_nan` /
  `is_inf` / `is_zero`, `abs`/negate/`copysign`/`select`. Arithmetic **replays**
  the recorded `fp<W>_<op>.empbc` builtins through the context.
- `BitVec_T<N>` — `& | ^ ~`, `== !=`, `select`, public-amount shifts,
  `slice`/`concat`, `as_uint`, indexing.

### Arithmetic kernels (`emp::kernel`)

The small, inlineable structured kernels (ripple add/sub, mux, comparators,
multiply, restoring division, `if_then_else`) live in namespace `emp::kernel` in
[numeric_kernels.h](../emp-tool/circuits/numeric_kernels.h), written against bare
`Ctx::Wire` (no per-bit `Ctx*`). They are LSB-first and size-optimal: one AND per
full adder (an N-bit add is N−1 ANDs); unsigned `<` is the borrow-out of a
subtract (one AND/bit). The value-type operators forward to them, passing the width
as a runtime argument, so the fixed-width and runtime-width (`N == 0`) integers
share one kernel set. `Float_T` is the opposite — every nontrivial op is an
`.empbc` replay.

### Sorting

[sort.h](../emp-tool/circuits/sort.h) provides data-oblivious `compare_swap(a, b)`
(ascending min/max via `<` + `select`) and `sort(values)` (a Batcher odd-even
mergesort network, any length) over any value with `operator<` and `select`
(`UInt_T` / `Int_T` / `Float_T`). The compare-swap schedule is fixed, so the gate
stream is identical for every input.

### Crypto primitives (`emp::circuit::crypto`)

BooleanContext-native crypto circuits over the value layer:

- [aes128.h](../emp-tool/circuits/crypto/aes128.h) — `aes_sbox`, `aes128_key_schedule`,
  `aes128_encrypt_block`, `aes128_encrypt`, plus `aes128_ctr` (CTR mode, NIST SP
  800-38A). The public boundary is `BitVec_T`: AES bytes are
  `BitVec_T<Ctx,8>`, blocks/keys/IVs are `BitVec_T<Ctx,128>`, expanded keys are
  `BitVec_T<Ctx,1408>`, and CTR maps `BitVec_T<Ctx,N>` to `BitVec_T<Ctx,N>`.
  Internal helpers keep bulk state as bare `Ctx::Wire` arrays. Other modes
  (CBC, …) are caller-composed from the block primitive.
- [sha256.h](../emp-tool/circuits/crypto/sha256.h) — `sha256_compress` (the word-level
  compression function over `UInt_T<Ctx,32>`) + `sha256(ctx, BitVec_T<Ctx,N>)`
  returning `BitVec_T<Ctx,256>` (full padded hash for a compile-time public N-bit
  message).
- [keccak.h](../emp-tool/circuits/crypto/keccak.h) — `keccak_f1600` (the lane-level
  permutation over `UInt_T<Ctx,64>[25]`) + `sha3_256(ctx, BitVec_T<Ctx,N>)`
  returning `BitVec_T<Ctx,256>`.

These are the source of truth; the prebuilt `aes128` / `sha256_256` /
`sha3_256_256` `.empbc` assets (loaded by `ir/builtins.h`) are kept as replay
fixtures and are checked against the kernels by `test_builtin_circuits`.

### Context check

Mixing typed values from two different contexts silently corrupts (especially with
id-based wires). `check_same_context` guards binary operators: a trivial pointer
compare of the two `context()`s. It is DEBUG-ONLY by default (on when `NDEBUG` is
unset); `-DEMP_CONTEXT_CHECKS=1` makes it an always-on `error()`,
`-DEMP_CONTEXT_CHECKS=0` disables it entirely.

## Bit / byte ordering

The toolkit-wide convention is **LSB-first within a byte, byte sequential in
memory**. It is the layout the canonical clear codecs use (`encode`/`decode`
emit/consume bits LSB-first) and the layout the crypto kernels and `.empbc` byte
feeds use. Two pieces:

1. **Bit-within-byte: LSB at index 0.** `(byte >> k) & 1` with `k=0` gives the
   least-significant bit. This matches FIPS-197's `b_0` = "low order bit" and the C
   language convention.
2. **Bytes sequential: byte `i` of the buffer fills bits `8i..8i+7`.** No byte
   reordering.

So bit `8` (LSB of byte 1) sits just past bit `7` (MSB of byte 0): the buffer is
one large little-endian multi-byte integer. On a little-endian host, bit `i` of a
fixed-width value equals bit `i` of the corresponding scalar directly — no
transformation; on a big-endian host you'd byte-swap before feeding.

### Exceptions / things that flip

- **Some published cryptographic circuits use MSB-first per-byte notation.** The
  Boyar–Peralta AES S-box formulas in
  [aes128.h](../emp-tool/circuits/crypto/aes128.h) (`emp::circuit::crypto::aes_sbox`) are
  written with `U[0]` = MSB of the byte. The function does a one-time index flip
  (`U[i] = U_lsb[7-i]`) at entry and exit so callers see the LSB-first convention.
  The flip emits zero gates — it's pure renaming.
