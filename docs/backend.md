# Backend layer (`emp-tool/execution/`)

Conventions for the protocol backends — `ClearBackend`, `HalfGate*`,
`PrivacyFree*`, and any new gate-engine you add. Read this when
writing a new `Backend` subclass or modifying gate-dispatch internals.

For circuit-side primitive conventions, read
[circuits.md](circuits.md).

## Global backend pointer

- Exactly one `Backend* backend` global (thread-local under
  `EMP_TOOL_THREADING`). Every gate, feed, reveal flows through it.

## Type erasure at the dispatch boundary

- `Backend` is type-erased: gate dispatch takes `void*` plus a virtual
  `wire_bytes()` oracle. This is the boundary between the templated
  circuit layer and the protocol; concrete backends reinterpret the
  pointers as their own `Wire`.

- `Bit_T<Wire>(bool, int)` asserts
  `backend->wire_bytes() == sizeof(Wire)`. The assert compiles out
  under NDEBUG.

## Bulk vs scalar gate paths

- Bulk variants (`and_gate_n`, `xor_gate_n`, `not_gate_n`) have
  loop-fallback defaults on the base class. Concrete backends override
  them when batching meaningfully amortizes per-gate cost. `BitVec_T`
  ops dispatch through the bulk API (one virtual call per BitVec op);
  `Bit_T` ops use the scalar path (per-bit dispatch).

## Feed / reveal: what stays at the base

- Backends whose `feed` / `reveal` need OT (HalfGate, PrivacyFree)
  leave the base no-op overrides in place. The OT-driven layers live
  downstream in emp-sh2pc / emp-ag2pc / emp-agmpc (which wrap input
  OT around the gate engine via `setup_*` helpers). emp-ot itself
  provides only the OT primitives those layers consume.
