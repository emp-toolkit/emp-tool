# Agent guide for emp-tool

Entry point for AI coding agents working on this repository. Read this
file first, then load only the subdocs relevant to your task.

## Project at a glance

emp-tool is the foundation of the EMP toolkit: a header-mostly C++
library providing the circuit primitives (`Bit` / `BitVec` /
`UnsignedInt` / `SignedInt` / `Float`), backend execution (clear /
half-gate / privacy-free), IO channels (`NetIO`, `NetIOBuffered`), and
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
| Write or modify a `test/*.cpp` file | [docs/test_conventions.md](docs/test_conventions.md) |
| Verify or debug a numeric corner case (wrap, division, shifts, resize) | [docs/numeric_semantics.md](docs/numeric_semantics.md) |

## Top-level rules (apply to all work)

These are short enough to live here so you don't have to load them
from a subdoc.

- When a benchmark looks too good to be true (e.g. < 1 cy/op for a
  multi-instruction primitive), suspect the compiler hoisted the call.
  Chain output to input.
- `random_data` (in PRG) requires 16-byte-aligned destinations and
  asserts. Use `random_data_unaligned` for stack ints, small structs,
  and any other call site that isn't naturally aligned.
