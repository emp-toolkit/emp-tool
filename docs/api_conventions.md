# Public API conventions

Cross-cutting conventions for emp-tool public APIs. Read this before
adding or changing an exported function that takes a byte/block/bool
count, before choosing a PRG buffer-fill API, or before writing a
failure path.

## Failure reporting: error(), never exceptions

Every failure in emp-tool — hostile/corrupt input (`.empbc` loaders,
`validate_program`), API misuse (shape/width mismatches), I/O setup and
mid-protocol faults, malicious-abort checks — reports through `error()`
(`runtime/core/utils.h`): print the message with the call site, then
`std::_Exit(1)`. emp-tool does not throw, anywhere, and is
`-fno-exceptions`-compatible.

Why one fatal path:

- Unwinding past half-settled protocol state (a partly garbled circuit,
  a half-consumed OT batch) is never safe to resume from, and `exit()`'s
  destructor/atexit cleanup races still-live ThreadPool workers
  (see `utils.hpp` for the `_Exit` rationale).
- Exceptions buy recoverability only if some caller can meaningfully
  recover; in a two-party protocol binary there is no such caller — the
  process is the session.
- One discipline keeps every failure observable the same way: nonzero
  exit + one stderr line with `file:line`.

Consequences for code and tests:

- Don't write cleanup that only runs on a failure path (close-on-error,
  free-on-error): `error()` ends the process, so the path is dead. Plain
  straight-line code with `error()` calls is the idiom.
- A "rejects bad input" test asserts child-process death, not a caught
  exception — fork, silence stderr, expect nonzero exit (see
  `test/ir/test_boolean_program.cpp`'s `dies()` helper).

## Bit buffer contract

EMP uses two bit-buffer shapes.

For typed values with compile-time width, codecs return:

```cpp
std::array<bool, V::width()>
```

This is the session-I/O contract for `WireValue`: width is part of the
type, storage is real `bool`, and `.data()` may be passed to APIs that
take `const bool*`.

For runtime-sized bit sequences, use byte-bools:

```cpp
std::vector<uint8_t>    // owning; the byte-bool codec returns this
const uint8_t*          // + length; the byte-bool codec reads this
```

Each byte represents one bit and must be normalized to `0` or `1`.

Do not use `std::vector<bool>` in emp-tool library/protocol code. It is
bit-packed, has proxy references, has no real `bool*`, and forces
hidden copies. Do not reinterpret byte-bool storage as `bool*`; convert
explicitly if an API requires real `bool` storage.

## Length and count parameters

Buffer-length and count parameters on emp-tool's public API use
`int64_t`, not `int` or `size_t`.

Why:

- `int` overflows at 2^31 elements.
- unsigned `size_t` underflows silently in `len -= batch` style loops
  that decrement to zero.
- `int64_t` avoids both and matches the existing IO / crypto APIs.

New emp-tool APIs that take a "number of bytes / blocks / bools /
points" parameter follow this convention. Internal counters and indices
inside those bodies should match:

```cpp
for (int64_t i = 0; i < len; ++i) {
    // ...
}
```

Template non-type parameters stay as `int` / `size_t` when they are
compile-time bounded sizes (`int N`, `int K`, `size_t Width`, etc.).

## PRG buffer fills

`PRG::random_data` requires a 16-byte-aligned destination and asserts
in debug builds. Use `PRG::random_data_unaligned` for stack integers,
small structs, byte buffers with arbitrary offset, and any other
destination that is not naturally `block`-aligned.

Use the aligned fast path only when the destination is known to be
16-byte aligned, such as a `block*` or a suitably aligned block buffer.
