# Project conventions for Claude

## Architecture conventions

### Circuit class layer (`emp-tool/circuits/`)

- Circuit primitives — `Bit_T`, `BitVec_T`, `UnsignedInt_T`,
  `SignedInt_T`, `Float_T` — are class templates on `Wire`. Class
  definitions must not reference `block`. Wire storage is inline
  (`Wire bit;` in `Bit_T`, `vector<Bit_T<Wire>> bits;` in `BitVec_T`)
  — required for the gate-rate budget; do not introduce indirection.

- The `block`-typed default aliases (`Bit`, `BitVec`, `UnsignedInt`,
  `SignedInt`, `Float`, plus the AES/SHA3 calculator aliases) live
  only in `emp-tool/emp-tool.h`. Custom-wire users include the
  templated headers directly and spell out their own aliases.

- `UnsignedInt_T` and `SignedInt_T` inherit from `BitVec_T` for storage
  and bitwise/structural ops. The derived classes override `& | ^ ~`
  and the static-shamt shifts so they return the derived type — keeps
  `UnsignedInt ^ UnsignedInt → UnsignedInt` rather than slicing into
  `BitVec`. Conversion between signed and unsigned is an explicit
  `.as_signed()` / `.as_unsigned()` bit-cast (no gates).

- Sortable / If / sort dispatch goes through the template-template
  mixin `Sortable<Wire, T>` where `T` is `template<typename> class T`.
  Derived classes supply `geq` / `equal` / `select`; the mixin provides
  `>=`, `<=`, `<`, `>`, `==`, `!=`, `If`. Don't add `operator==` on
  `BitVec_T` itself — it would collide with the mixin's operator==
  on classes that inherit both `BitVec_T` and `Sortable`.

- Shared bit-array kernels (`add_full`, `sub_full`, `mul_full`,
  `div_full`, `cond_neg`, `if_then_else`) live in
  `circuits/numeric_kernels.h`. Both `UnsignedInt_T` and `SignedInt_T`
  consume them. Sign semantics live one level up: signed division is
  unsigned div with pre/post `cond_neg`, signed comparison is
  sign-extended subtraction, etc.

### Backend layer (`emp-tool/execution/`)

- Exactly one `Backend* backend` global (thread-local under
  `EMP_TOOL_THREADING`). Every gate, feed, reveal flows through it.
- `Backend` is type-erased: gate dispatch takes `void*` plus a virtual
  `wire_bytes()` oracle. This is the boundary between the templated
  circuit layer and the protocol; concrete backends reinterpret the
  pointers as their own `Wire`.
- Bulk variants (`and_gate_n`, `xor_gate_n`, `not_gate_n`) have
  loop-fallback defaults on the base class. Concrete backends override
  them when batching meaningfully amortizes per-gate cost. `BitVec_T`
  ops dispatch through the bulk API (one virtual call per BitVec op);
  `Bit_T` ops use the scalar path (per-bit dispatch).
- `Bit_T<Wire>(bool, int)` asserts
  `backend->wire_bytes() == sizeof(Wire)`. The assert compiles out
  under NDEBUG.
- Backends whose `feed` / `reveal` need OT (HalfGate, PrivacyFree)
  leave the base no-op overrides in place. The OT-aware subclass lives
  downstream in emp-ot.

### IO channel layer (`emp-tool/io/`)

- Two NetIO implementations, same `IOChannel` contract: `NetIO` (default,
  `"wb"` stdio + raw `::read` on recv — faster on small-message protocol
  workloads) and `NetIOBuffered` (`"wb+"` stdio + `fread` on both paths
  — faster on multi-MiB bulk transfers). Pick `NetIO` unless you're
  specifically pushing bulk data; the rest of this section applies to
  both.

- Flush contract: callers MUST call `flush()` at the end of any protocol
  step that ends in sends, before returning to the caller or blocking on
  anything other than a recv on the same NetIO. The auto-flush only fires
  when the same NetIO does a recv (`flush()` at the top of
  `recv_data_internal`); a NetIO whose step is purely sends — e.g. the
  IKNP receiver-role channel during `setup_recv` — strands its tail bytes
  in the user-space `send_buf` until something else moves them.

- `~NetIO` flushes, so pure send-then-destruct works. Long-lived NetIOs
  with a mid-life send-only step do not: the destructor only runs at
  end-of-life, which doesn't happen until the protocol completes, which
  can't complete because the peer is blocked on bytes stuck in send_buf.
  This is a circular-wait deadlock, not "data eventually arrives slowly".

- Rule of thumb: if the next thing you do on this NetIO isn't a recv,
  flush first. Applies to function boundaries, phase boundaries within
  a function, and any blocking wait (thread join, barrier, recv on a
  *different* NetIO).

- `test/netio.cpp` is templated on the IO type and runs the full
  correctness + send-only-regression + bench suite against both classes
  back-to-back; `docs/netio-flush-deadlock-investigation.md` has the
  investigation that turned this from stdio trivia into a hard rule.

- Thread-safety: NetIO and NetIOBuffered are NOT thread-safe. The
  user-space `send_buf` coalescing has no per-call lock; concurrent
  `send_data` / `recv_data` / `flush` from two threads on the same
  instance corrupts the buffer. Each instance must be owned by one
  thread at a time — `flush()` counts as a "touch" and is unsafe to
  call from a thread other than the one currently sending. (The
  pre-rewrite NetIO didn't have this hazard because every send went
  through `fwrite`, which serializes via stdio's `flockfile`. The
  user-space buffer is faster but loses that implicit serialization.)

- Debug build assertion: under `!NDEBUG`, NetIO and NetIOBuffered carry
  an `_in_use` atomic counter and `touch_guard` that wraps
  `send_data_internal` / `recv_data_internal` / `flush`. If two threads
  enter any of those on the same instance simultaneously, the build
  aborts with `NetIO race: concurrent <op> on the same NetIO`. Zero
  cost under `-DNDEBUG`. Use this when a multi-party protocol behaves
  flakily — race or no race, the answer falls out of a Debug build.

### Numeric semantics

These are normative — match exactly what the corresponding C++ native
integer would produce on commodity hardware.

- `+`, `-`, `*` on `UnsignedInt_T` and `SignedInt_T` wrap mod 2^N.
  Unsigned matches `uint{N}_t` directly; signed matches `int{N}_t`
  *as implemented on hardware* (two's-complement wrap). C's
  signed-overflow UB is sidestepped — emp-tool wraps deterministically.
- Signed `/` truncates toward zero (C99+). Signed `%` carries the sign
  of the dividend. `INT_MIN / -1` is allowed and produces `INT_MIN`
  (the bit-pattern result of two's-complement division). Callers
  needing a different policy must check explicitly. Division by zero
  is undefined behavior of the circuit; do not pass it.
- `<<` is logical on every type. `>>` is logical on `BitVec_T` and
  `UnsignedInt_T`, arithmetic (sign-fill) on `SignedInt_T`. Shift
  amounts ≥ width yield zero on logical ops, or sign-fill on signed
  `>>`. Both static-shamt and dynamic-shamt forms exist.
- `resize(W)` zero-extends on `UnsignedInt_T`, sign-extends on
  `SignedInt_T`. Truncation drops the high bits.

## Test files for emp-tool primitive headers

### One file per component

Each primitive header (under `emp-tool/core/`, `emp-tool/crypto/`,
`emp-tool/group/`, `emp-tool/io/`, `emp-tool/circuits/`) has exactly
one corresponding file in `test/`, named after the header
(e.g. `crypto/f2k.h` → `test/f2k.cpp`). The numeric circuit headers
use abbreviated names: `circuits/unsigned_int.h` → `test/uint.cpp`,
`circuits/signed_int.h` → `test/int.cpp`, `circuits/bitvec.h` →
`test/bitvec.cpp`.

No separate `_bench.cpp` companions — example, correctness, and
benchmarks all live in the same file. CMake registers it as a single
`add_test_case`.

### Required structure

Each file is laid out top-to-bottom as a tutorial. Three sections, in order:

1. **`example()`** — short, readable demonstrations of the public API.
   Treat this as documentation: idiomatic variable names, brief printed
   output that shows what each primitive returns. Keep it 5–10 lines per
   primitive at most. The example is the headline; everything below
   supports it.

2. **`run_correctness()`** — verification, ideally against an external
   ground truth.
   - For `aes.h`: NIST FIPS-197 test vectors **and** OpenSSL cross-check.
   - For `f2k.h`: scalar-reference GF(2^128) multiply implementation **and**
     algebraic identities (a·0=0, a·1=a, commutativity, distributivity).
   - For `int.h` / `uint.h` / `bitvec.h`: random fuzzing against
     `int{N}_t` / `uint{N}_t` ground truth, **plus** boundary cases
     (0, ±1, MAX, MIN, MAX±1, MIN±1, MAX/2, shamt > width). Both are
     required, not optional.
   - For others: hand-rolled reference loop, round-trip checks, or
     known-answer tests. Pick the strongest available.
   Each check function returns `bool`; a single dispatcher prints `OK`/`FAIL`
   per primitive, returns `false` on any failure, and `main()` exits 1 on
   correctness failure.

3. **`bench(double sec)`** — comprehensive throughput, accepting
   per-row seconds from `argv[1]` (default 0.2 or 0.3).
   - **Single-shot ops** (e.g. `gfmul`, `mul128`, `reduce`, AES
     `set_key`): chain output back into input so each call has a real
     serial dependency on the previous. Otherwise the compiler hoists
     the loop-invariant call out and "cy/op" reflects only the asm
     clobber. Report `Mops` and `cy/op @3GHz` (notional 3GHz
     normalization, not a hardware claim).
   - **Vector ops over a size parameter**: sweep N over a representative
     range (e.g. `{16, 64, 256, 1024, 4096, 16384, 65536}`) so the
     reader can see where the routine becomes bandwidth-bound vs
     compute-bound. Report `GiB/s` of input touched and `cy/blk @3GHz`.
   - **Fixed-size ops** (e.g. `packing(N=128)`): single row, report
     `Mops` and `cy/op @3GHz`.
   - Use `__attribute__((target("sse2")))` on x86 where the timing
     primitives need SSE.

### Bench harness skeleton

```cpp
template <typename Fn>
static double run_for(double seconds, Fn &&fn, void *clob) {
    for (int i = 0; i < 32; ++i) {
        fn();
        asm volatile("" : "+m"(*(char *)clob) : : "memory");
    }
    int64_t iters = 64;
    while (true) {
        auto a = clk::now();
        for (int64_t i = 0; i < iters; ++i) {
            fn();
            asm volatile("" : "+m"(*(char *)clob) : : "memory");
        }
        double el = chrono::duration<double>(clk::now() - a).count();
        if (el >= seconds) return double(iters) / el;
        iters *= 2;
    }
}
```

The `clob` pointer must be alive through the iteration; clobbering it
via inline asm prevents dead-code elimination of the bench body.

### Header comment

Open every test file with a short comment listing the API surface of the
header it tests, so readers can scan without opening the header:

```cpp
// <subdir>/<name>.h — <one-line purpose>. Read example() first; the rest
// is verification + throughput.
//
// What's in <name>.h:
//   func1(...)        one-line purpose
//   func2(...)        one-line purpose
//   ...
```

## Other notes

- API breakage is acceptable when modernizing — downstream emp-* repos
  can be updated as needed. Don't keep dead wrappers just for legacy
  callers if no in-tree code uses them.
- Don't pad tests with edge cases that no caller hits; cover what the
  API actually has to handle. The exception is the numeric classes
  above, where boundary coverage *is* the contract.
- When a benchmark looks too good to be true (e.g. < 1 cy/op for a
  multi-instruction primitive), suspect the compiler hoisted the call.
  Chain output to input.
- `random_data` (in PRG) requires 16-byte-aligned destinations and
  asserts. Use `random_data_unaligned` for stack ints, small structs,
  and any other call site that isn't naturally aligned.
