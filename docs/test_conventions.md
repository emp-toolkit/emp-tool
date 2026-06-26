# Test file conventions

How `test/<layer>/*.cpp` files are structured. Read this when writing a new
test file for a header under `emp-tool/runtime/core/`, `emp-tool/runtime/crypto/`,
`emp-tool/runtime/io/`, or `emp-tool/circuits/`, or when modifying an existing
test.

## One file per component

Each primitive header has exactly one corresponding file under `test/<layer>/`,
named `test_<header>.cpp`, where `<layer>` mirrors the source layer —
`test/runtime/`, `test/ir/`, or `test/circuits/` (e.g. `crypto/f2k.h` →
`test/runtime/test_f2k.cpp`).
Binaries land flat at `build/test_<header>` (`RUNTIME_OUTPUT_DIRECTORY` is the
build root — no `build/test/` traversal).
The numeric circuit headers use abbreviated names:
`circuits/unsigned_int.h` → `test/circuits/test_uint.cpp`,
`circuits/signed_int.h` → `test/circuits/test_int.cpp`,
`circuits/bitvec.h` → `test/circuits/test_bitvec.cpp`.

Throughput benchmarks live separately under `bench/`. CMake registers
tests with `add_test_case` / `add_test_case_with_run`; benchmark
targets are not registered with `ctest`.

## Required structure

Each file is laid out top-to-bottom as a tutorial. Two sections, in
order:

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
- For `signed_int.h` / `unsigned_int.h` / `bitvec.h`: random fuzzing against
  `int{N}_t` / `uint{N}_t` ground truth, **plus** boundary cases
  (0, ±1, MAX, MIN, MAX±1, MIN±1, MAX/2, shamt > width). Both are
  required, not optional.
- For others: hand-rolled reference loop, round-trip checks, or
  known-answer tests. Pick the strongest available.

Each check function returns `bool`; a single dispatcher prints
`OK` / `FAIL` per primitive, returns `false` on any failure, and
`main()` exits 1 on correctness failure.

Throughput checks belong in `bench/`; see
[benchmark_conventions.md](benchmark_conventions.md).

## Header comment

Open every test file with a short comment listing the API surface of
the header it tests, so readers can scan without opening the header:

```cpp
// <subdir>/<name>.h — <one-line purpose>. Read example() first; the rest
// is verification.
//
// What's in <name>.h:
//   func1(...)        one-line purpose
//   func2(...)        one-line purpose
//   ...
```
