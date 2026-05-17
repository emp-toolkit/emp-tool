# Agent guide for emp-tool

Entry point for AI coding agents working on this repository. Read this
file first, then load only the subdocs relevant to your task.

## Project at a glance

emp-tool is the foundation of the EMP toolkit: a header-mostly C++
library providing the circuit primitives (`Bit` / `BitVec` /
`UnsignedInt` / `SignedInt` / `Float`), backend execution (clear /
half-gate / privacy-free), IO channels (`NetIO`, `TLSIO`), and
crypto utilities (AES, hash, PRG, group ops) that the higher-level
emp-ot, emp-sh2pc, emp-ag2pc, emp-agmpc protocols build on.

Standard users include `emp-tool/emp-tool.h` for the `block`-typed
default aliases. Custom-wire users include the templated headers
under `emp-tool/circuits/` and spell out their own aliases.

## When to read what

Pick the smallest set of subdocs that covers your task. Each is
self-contained and assumes you've read this index.

| Task | Subdoc(s) |
|---|---|
| Modify a circuit primitive header (`Bit_T`, `BitVec_T`, `UnsignedInt_T`, `SignedInt_T`, `Float_T`) | [docs/circuits.md](docs/circuits.md) + [docs/numeric_semantics.md](docs/numeric_semantics.md) |
| Write or modify a `Backend` subclass (gate dispatch, garbling) | [docs/backend.md](docs/backend.md) |
| Write protocol code that uses NetIO (sends, recvs, multi-thread, flush) | [docs/io_channel.md](docs/io_channel.md) |
| Investigate a NetIO deadlock | [docs/io_channel.md](docs/io_channel.md) |
| Translate ordinary C++ / Python to an EMP secure circuit | [docs/EMP_TRANSLATION.md](docs/EMP_TRANSLATION.md) |
| Write or modify a `test/test_*.cpp` file | [docs/test_conventions.md](docs/test_conventions.md) |
| Verify wire-byte equivalence after a refactor / optimization (deterministic PRG, `TraceIO`) | [docs/test_mode.md](docs/test_mode.md) |
| Verify or debug a numeric corner case (wrap, division, shifts, resize) | [docs/numeric_semantics.md](docs/numeric_semantics.md) |
| Convert between byte buffers and `BitVec` / `Bit[]`, or debug an endianness mismatch | [docs/circuits.md § Bit / byte ordering](docs/circuits.md) |

## Top-level rules (apply to all work)

These are short enough to live here so you don't have to load them
from a subdoc.

- When a benchmark looks too good to be true (e.g. < 1 cy/op for a
  multi-instruction primitive), suspect the compiler hoisted the call.
  Chain output to input.
- `random_data` (in PRG) requires 16-byte-aligned destinations and
  asserts. Use `random_data_unaligned` for stack ints, small structs,
  and any other call site that isn't naturally aligned.
- Buffer-length and count parameters on emp-tool's public API use
  `int64_t`, not `int` or `size_t`. `int` overflows at 2^31 elements;
  unsigned `size_t` underflows silently in `len -= batch` style loops
  that decrement to zero. `int64_t` avoids both. New emp-tool APIs
  that take a "number of bytes / blocks / bools / points" parameter
  follow this convention. Internal counters and indices in the bodies
  match (`int64_t i = 0; i < len; ++i`). Template non-type parameters
  (`int N`, `int K` etc.) stay as `int` since they're compile-time
  bounded small constants.
