# Test file conventions for emp-tool primitive headers

How `test/*.cpp` files are structured. Read this when writing a new
test file for a header under `emp-tool/core/`, `emp-tool/crypto/`,
`emp-tool/io/`, or `emp-tool/circuits/`, or when modifying an existing
test.

## One file per component

Each primitive header has exactly one corresponding file in `test/`,
named `test_<header>.cpp` (e.g. `crypto/f2k.h` → `test/test_f2k.cpp`).
Binaries land at `build/test_<header>` (no `build/test/` traversal).
The numeric circuit headers use abbreviated names:
`circuits/unsigned_int.h` → `test/test_uint.cpp`,
`circuits/signed_int.h` → `test/test_int.cpp`,
`circuits/bitvec.h` → `test/test_bitvec.cpp`.

No separate `_bench.cpp` companions — example, correctness, and
benchmarks all live in the same file. CMake registers it as a single
`add_test_case`.

## Required structure

Each file is laid out top-to-bottom as a tutorial. Three sections,
in order:

### 1. `example()`

Short, readable demonstrations of the public API. Treat this as
documentation: idiomatic variable names, brief printed output that
shows what each primitive returns. Keep it 5–10 lines per primitive at
most. The example is the headline; everything below supports it.

### 2. `run_correctness()`

Verification, ideally against an external ground truth.

- For `aes.h`: NIST FIPS-197 test vectors **and** OpenSSL cross-check.
- For `f2k.h`: scalar-reference GF(2^128) multiply implementation
  **and** algebraic identities (a·0=0, a·1=a, commutativity,
  distributivity).
- For `int.h` / `uint.h` / `bitvec.h`: random fuzzing against
  `int{N}_t` / `uint{N}_t` ground truth, **plus** boundary cases
  (0, ±1, MAX, MIN, MAX±1, MIN±1, MAX/2, shamt > width). Both are
  required, not optional.
- For others: hand-rolled reference loop, round-trip checks, or
  known-answer tests. Pick the strongest available.

Each check function returns `bool`; a single dispatcher prints
`OK` / `FAIL` per primitive, returns `false` on any failure, and
`main()` exits 1 on correctness failure.

### 3. `bench(double sec)`

Comprehensive throughput, accepting per-row seconds from `argv[1]`
(default 0.2 or 0.3).

- **Single-shot ops** (e.g. `gfmul`, `mul128`, `reduce`, AES
  `set_key`): chain output back into input so each call has a real
  serial dependency on the previous. Otherwise the compiler hoists
  the loop-invariant call out and "cy/op" reflects only the asm
  clobber. Report `Mops` and `cy/op @3GHz` (notional 3 GHz
  normalization, not a hardware claim).
- **Vector ops over a size parameter**: sweep N over a representative
  range (e.g. `{16, 64, 256, 1024, 4096, 16384, 65536}`) so the
  reader can see where the routine becomes bandwidth-bound vs
  compute-bound. Report `GiB/s` of input touched and `cy/blk @3GHz`.
- **Fixed-size ops** (e.g. `packing(N=128)`): single row, report
  `Mops` and `cy/op @3GHz`.
- Use `__attribute__((target("sse2")))` on x86 where the timing
  primitives need SSE.

## Bench harness skeleton

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

## Header comment

Open every test file with a short comment listing the API surface of
the header it tests, so readers can scan without opening the header:

```cpp
// <subdir>/<name>.h — <one-line purpose>. Read example() first; the rest
// is verification + throughput.
//
// What's in <name>.h:
//   func1(...)        one-line purpose
//   func2(...)        one-line purpose
//   ...
```
