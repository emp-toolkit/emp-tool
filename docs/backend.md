# Execution layer (`emp-tool/runtime/execution/`)

emp-tool's circuits are evaluated by a **`BooleanContext`** — a type with a cheap,
copyable `Wire` and value-return `public_bit` / `and_gate` / `xor_gate` /
`not_gate`. There is no global backend and no `void*` virtual dispatch: a context
is passed explicitly and the gate ops are statically dispatched and inlineable. See
[circuits.md](circuits.md) for the value layer and `ir/context/context.h` for the
concept and the built-in analysis contexts (`ClearCtx` / `CountCtx` / `DigestCtx` /
`RecordCtx`).

## Implementing a backend: a pure context + a session

A protocol provides two pieces. The **context** is pure circuit execution — only
the four gate ops, no I/O — like emp-sh2pc's `SH2PCCtx`, or the shared
`ChunkRecorderCtx` that emp-ag2pc / emp-agmpc expose as `ctx_t`:

- pick a `Wire` (a garbled label, a wire id, …) that is `std::regular`;
- implement the four gate ops (eager crypto, or recording into an IR — whatever the
  protocol does per gate); optionally `and_many` for batched AND layers.

The **session** owns the clear↔circuit I/O boundary and the protocol state (IO,
party, OT/preprocessing, batching), wrapping a context — like emp-tool's
`ClearSession` (`session/clear_session.h`):

- hold the context and expose `direct_ctx()` for value/context-level work
  (the `DirectSession` concept, `ir/session/session.h`);
- expose `input<T>(owner, clear)` (and `input_batch` where applicable) /
  `reveal(value, recipient)`, implemented over the value's `WireValue`
  codec (`T::width()` / `T::encode` / `T::decode`, `ir/wire_value.h`).
  `reveal` returns `std::optional<clear_t>`: the value on a party that learns it
  (every party for `PUBLIC`, the named recipient otherwise) and `std::nullopt` on a
  party that does not. A plaintext `ClearSession` always populates it.

The context automatically works with the [frontend](frontend.md) — live `run`,
`compile`, and replay over any circuit value — and with the analysis contexts, with
no frontend changes. Pure circuit bodies take and return values and never touch
I/O; only the session does. That uniformity is the payoff of the seam.

## Garbling primitives (`execution/`)

The execution layer ships the per-gate garble/evaluate primitives a garbled context
builds on, as free functions over `block` labels (XOR/NOT are free; only AND needs
ciphertext):

- [half_gate.h](../emp-tool/runtime/execution/half_gate.h) — `halfgates_garble` /
  `halfgates_eval` (Zahur–Rosulek–Evans half-gates).
- [privacy_free.h](../emp-tool/runtime/execution/privacy_free.h) — `privacy_free_garble` /
  `privacy_free_eval`.

`SH2PCCtx` calls `halfgates_garble`/`halfgates_eval` directly inside its `and_gate`;
a new garbled context does the same. The OT that feeds private inputs lives in the
protocol libraries (emp-sh2pc / emp-ag2pc / emp-agmpc) over emp-ot.
