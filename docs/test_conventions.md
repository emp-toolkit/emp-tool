# Test file conventions

How `test/<layer>/*.cpp` files are structured. Read this when writing a new
test file for a header under `emp-tool/runtime/core/`, `emp-tool/runtime/crypto/`,
`emp-tool/runtime/io/`, `emp-tool/ir/`, `emp-tool/circuits/`, or
`emp-tool/circuits/frontend/`, or when modifying an existing test.

## One file per component

Each primitive header gets one corresponding file under `test/<layer>/`,
named `test_<header>.cpp`, where `<layer>` mirrors the source layer —
`test/runtime/`, `test/ir/`, or `test/circuits/` (e.g. `crypto/f2k.h` →
`test/runtime/test_f2k.cpp`). This is a convention, not an enforced
invariant; known gap: `runtime/execution/privacy_free_*` has no dedicated
test file (the registered execution test covers half-gates).
Binaries land flat at `build/test_<header>` (`RUNTIME_OUTPUT_DIRECTORY` is the
build root — no `build/test/` traversal).
The numeric circuit headers use abbreviated names:
`circuits/unsigned_int.h` → `test/circuits/test_uint.cpp`,
`circuits/signed_int.h` → `test/circuits/test_int.cpp`,
`circuits/bitvec.h` → `test/circuits/test_bitvec.cpp`.

Throughput benchmarks live separately under `bench/`. CMake registers
tests with `add_test_case` / `add_test_case_with_run` /
`add_test_case_cxx20` (functionally identical to `add_test_case`; the
distinct name self-documents that the file exercises the C++20
`BooleanContext`/typed-value surface — see `test/CMakeLists.txt`);
benchmark targets are not registered with `ctest`.

## Required structure

Every file is a tutorial read top-to-bottom: a demonstration of the
public API first, verification second. The common contract is the exit
code — a test process exits 0 only if every check passed. Within that,
three shapes recur.

- **`test/runtime/` single-process primitive tests** (aes, block,
  ccrh, f2k, hash, mitccrh, prg, prp, test_mode, utils) use an
  `example()` function followed by a `bool`-returning
  `run_correctness()` dispatcher that prints `OK` / `FAIL` per check
  and lets `main()` exit 1 on any failure.
- **Most `test/ir/` and `test/circuits/` tests** use section functions
  (`sweep_*`, etc., with `example()` where present) called from
  `main()`, each recording failures through a `check()` / `chk()`
  helper into a fail counter that `main()` normalizes to a 0/1 exit.
- **Two-party `test/runtime/` io tests** (`test_netio`, `test_tlsio`)
  drive a `void run_correctness(IO*, party, …)` that fails fast through
  `expecting()` instead of returning `bool`, and have no `example()`.

A few older runtime files (`test_ro`, `test_ecc`, `test_halfgate`) are
flat `main()`-plus-assert. Use the first shape for a genuinely new
single-process primitive test.

### The demonstration (`example()`)

Short, readable demonstrations of the public API. Treat this as
documentation: idiomatic variable names, brief printed output that
shows what each primitive returns. Keep it 5–10 lines per primitive at
most. The example is the headline; everything below supports it.

### The verification

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
