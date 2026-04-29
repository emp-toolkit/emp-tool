# Project conventions for Claude

## Test files for `emp-tool/utils/*.h`

### One file per component

Each header in `emp-tool/utils/` has exactly one corresponding file in
`test/`, named after the header (e.g. `utils/f2k.h` → `test/f2k.cpp`). No
separate `_bench.cpp` companions — example, correctness, and benchmarks all
live in the same file. CMake registers it as a single `add_test_case`.

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
   - For others: hand-rolled reference loop, round-trip checks, or
     known-answer tests. Pick the strongest available.
   Each check function returns `bool`; a single dispatcher prints `OK`/`FAIL`
   per primitive, returns `false` on any failure, and `main()` exits 1 on
   correctness failure.

3. **`bench(double sec)`** — comprehensive throughput, accepting
   per-row seconds from `argv[1]` (default 0.2 or 0.3).
   - **Single-shot ops** (e.g. `gfmul`, `mul128`, `reduce`, AES
     `set_key`): chain output back into input so each call has a real
     serial dependency on the previous. Otherwise the compiler hoists the
     loop-invariant call out and "cy/op" reflects only the asm clobber.
     Report `Mops` and `cy/op @3GHz`.
   - **Vector ops over a size parameter**: sweep N over a representative
     range (e.g. `{16, 64, 256, 1024, 4096, 16384, 65536}`) so the reader
     can see where the routine becomes bandwidth-bound vs compute-bound.
     Report `GiB/s` of input touched and `cy/blk @3GHz`.
   - **Fixed-size ops** (e.g. `packing(N=128)`): single row, report `Mops`
     and `cy/op @3GHz`.
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

The `clob` pointer must be alive through the iteration; clobbering it via
inline asm prevents dead-code elimination of the bench body.

### Header comment

Open every test file with a short comment listing the API surface of the
header it tests, so readers can scan without opening the header:

```cpp
// utils/<name>.h — <one-line purpose>. Read example() first; the rest is
// verification + throughput.
//
// What's in <name>.h:
//   func1(...)        one-line purpose
//   func2(...)        one-line purpose
//   ...
```

### Other notes

- API breakage is acceptable when modernizing — downstream emp-* repos can
  be updated as needed. Don't keep dead wrappers just for legacy callers
  if no in-tree code uses them.
- Don't pad tests with edge cases that no caller hits; cover what the API
  actually has to handle.
- When a benchmark looks too good to be true (e.g. < 1 cy/op for a
  multi-instruction primitive), suspect the compiler hoisted the call.
  Chain output to input.
