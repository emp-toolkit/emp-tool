# Circuit frontend (`emp-tool/circuits/frontend/`)

A small, optional layer for **writing a pure circuit once and running it on any
context** — plaintext, garbled 2PC, ZK, … . You write the circuit in ordinary
typed circuit-value code (`Bit_T<Ctx>`, `UInt_T<Ctx,N>`, `Int_T<Ctx,N>`,
`Float_T<Ctx,W>`); the frontend lets you call it live, or `compile` it once into
a reusable, **context-free** `Circuit` and `run` it on whatever context you hold
— with no global backend and no per-bit virtual dispatch.

Everything lives in namespace **`emp::frontend`** (so `run` / `compile` don't
pollute `emp`). Header-only over emp-tool; **C++20**. Pull it in with
`#include "emp-tool/circuits/frontend/frontend.h"` (or directly
`#include "emp-tool/circuits/frontend/circuit_fn.h"`).

To **reuse** a compiled circuit across many calls without materializing each copy
(replay one unit by reference instead of inlining it), see
[composition.md](composition.md) — the `run` / `run_compose` inline-vs-reuse pattern.

This is the BooleanContext frontend: it compiles and replays over the typed
context-bound values (`Bit_T<Ctx>` / `UInt_T<Ctx,N>` / …), with the context
passed explicitly and no global backend.

For the typed values it builds on, read [circuits.md](circuits.md); for the
gate-context concept it replays over, read the header of `ir/context/context.h`;
for the session that owns I/O around it, read `ir/session/session.h`.

## What a circuit is — a pure function over a context

A circuit body takes typed circuit values as **arguments** and returns a typed
value; it does **no I/O of its own** (no `input`/`reveal`/OT). This is enforced
structurally: a body is recorded through a `RecordCtx`, which has no I/O.

- **No secret input inside** — pass secret/party inputs as arguments.
- **No `reveal` inside** — reveal the returned value *outside*, through the session.
- **Public constants inside are fine** — `a.constant(5)` (implicit form) or
  `UInt_T<Ctx,N>::constant(ctx, 5)` (explicit form) fold to constant gates. A
  value that may differ across parties or runs must be an **argument**.

I/O stays the session's job, around the circuit:

```cpp
ClearSession sess;                                    // session owns the I/O boundary
using Ctx = ClearSession::ctx_t;                      // a protocol session (e.g. SH2PCSession) exposes the same surface
using UInt32 = UInt_T<Ctx, 32>;
auto a = sess.input<UInt32>(ALICE, av);               // session feeds inputs
auto b = sess.input<UInt32>(BOB,   bv);
auto c = frontend::run(sess.ctx(), circuit, a, b);          // pure replay over the context
uint32_t r = sess.reveal<uint32_t>(c, PUBLIC).value(); // session reveals -> std::optional<uint32_t>
```

## Body forms

A body comes in two forms; `compile`/`run` detect which is invocable. A body
callable in **both** is a contract error (disambiguate).

- **implicit context** (default) — the typed values carry their own context, so
  the body needs none; make a constant from an argument with `a.constant(v)`:

  ```cpp
  auto add = [](auto a, auto b) { return a + b; };
  ```

- **explicit context** (general) — the body takes `Ctx&` first. Required for
  **nullary** circuits and for making a constant with no argument to anchor on:

  ```cpp
  auto bias = [](auto& ctx) {
      using C = std::remove_reference_t<decltype(ctx)>;
      return UInt_T<C,16>::constant(ctx, 4242);
  };
  ```

  The context parameter must be a mutable lvalue reference (`Ctx&` or
  `auto&`).

### The calling contract

One diagnostic site (`circuit_fn_traits` / `circuit_contract` in `circuit_fn.h`):
a body must be **callable with prvalue circuit-value arguments** and **return a
circuit value by value**. Arguments are passed by value, so a body cannot mutate
them (a non-`const` lvalue-reference parameter is rejected). Returning a
**reference**, returning **void**, returning a **non-circuit** value, taking a
non-circuit argument, or being callable in **both** context forms are all
compile-time errors with a precise message — in `compile<ArgVs...>` and live
`run` alike.

Live `run` materializes decayed argument values before invocation, matching
`compile`; an overload on lvalue versus rvalue arguments therefore cannot
describe different live and compiled circuits. Public `compile`, `run`, and
`compose` boundaries also check that arguments and results belong to the
context instance being used. These checks are O(arguments).

A circuit value is anything satisfying the `WireBundle` concept
(`ir/wire_value.h`): it exposes `Wire`, `context_type`, a positive static `width()`,
`context()`, `pack_wires` / `from_wires`, and a `rebind<Ctx>` that maps the same
value family onto another context. Adding the clear codec (`clear_t` /
`encode` / `decode`) makes it the stronger `WireValue` — the form session I/O
needs; the frontend itself requires only `WireBundle` (a clear codec is not
needed to compile/run). The five built-in families (`Bit_T`, `BitVec_T`,
`UInt_T`, `Int_T`, `Float_T` in `circuits/typed.h`) model `WireBundle` at positive
fixed width; `Bit_T` / `BitVec_T` / `Float_T` also model `WireValue` at each supported positive width,
while `UInt_T` / `Int_T` model `WireValue` only for width <= 64 (their clear
codecs ride 64-bit integers) — use `BitVec_T` for typed session I/O past 64 bits.
The runtime-width
forms `UInt_T<Ctx,0>` / `Int_T<Ctx,0>` (width chosen at construction)
intentionally do **not** model `WireBundle` — they have no *static* `width()` —
so they cannot be a `compile` / `run` argument; convert to a fixed
`UInt_T<Ctx,N>` (`to_fixed<N>()`) first if a runtime-width result must enter the
frontend. (They remain session-I/O eligible through the runtime-width
`input` / `reveal` overloads — they model `RuntimeWidthValue`.)

## Recording value types — context-free signatures

`compile` is parameterized by the **circuit value types over the recording
context** (`RecordCtx`). The `emp::rec::` aliases (`circuits/frontend/rec.h`) name those
types without spelling `RecordCtx`:

| value (per context)  | recording alias (`emp::rec::`)         |
|----------------------|----------------------------------------|
| `Bit_T<Ctx>`         | `rec::Bit`    (`= Bit_T<RecordCtx>`)    |
| `UInt_T<Ctx,N>`      | `rec::UInt<N>` (`= UInt_T<RecordCtx,N>`) |
| `Int_T<Ctx,N>`       | `rec::Int<N>`  (`= Int_T<RecordCtx,N>`)  |
| `Float_T<Ctx,W>`     | `rec::Float<W>` (`= Float_T<RecordCtx,W>`) |
| `BitVec_T<Ctx,N>`    | `rec::BitVec<N>` (`= BitVec_T<RecordCtx,N>`) |

The metadata a compiled signature needs — bit width and the per-context
family map — lives on the value type itself (`width()`, `rebind<Ctx>`).
The clear codec (`clear_t`, `encode`/`decode`) is NOT part of the compiled
signature (compile/run needs only `WireBundle`, as above); it is used at
the session I/O boundary, and where present it is exposed uniformly
through `emp::value_traits<T>` (`circuits/value_traits.h`):
`value_traits<T>::width()`, `value_traits<T>::encode(v)`,
`value_traits<T>::decode(bits)`, `value_traits<T>::rebind<Ctx>`. A value's
`rebind<Ctx>` re-attaches a context (`UInt_T<RecordCtx,32>::rebind<ClearCtx> ==
UInt_T<ClearCtx,32>`); `run` uses it to reconstruct the live result type.

## compile / run

```cpp
#include "emp-tool/circuits/frontend/circuit_fn.h"
#include "emp-tool/circuits/frontend/rec.h"
#include "emp-tool/ir/session/clear_session.h"
namespace cf = emp::frontend;
using namespace emp;

auto add  = [](auto a, auto b) { return a + b; };
auto circ = cf::compile<rec::UInt<32>, rec::UInt<32>>(add);   // record ONCE

ClearSession sess;                                            // run on any session's context
using Ctx = ClearSession::ctx_t;
auto x = sess.input<UInt_T<Ctx,32>>(ALICE, 7);
auto y = sess.input<UInt_T<Ctx,32>>(BOB,   5);
auto z = cf::run(sess.ctx(), circ, x, y);                     // replay -> UInt_T<Ctx,32>
```

- **compiled** — `compile<ArgVs...>(body)` records the body once through a
  `RecordCtx` and returns a `Circuit<RetV, ArgVs...>` (the `ArgVs` are value
  types over `RecordCtx`; use the `rec::` aliases). `run(ctx, circ, args...)`
  replays it on the live `ctx` (args **by const-ref**, rebound to that ctx; the
  context is **explicit** — no global backend). The same `Circuit` runs
  identically on the plaintext session's context, `SH2PCCtx`, etc. — user circuits are as portable as
  the built-in `.empbc` files.
- **live** — `run(body, args...)` invokes the body directly on already-live typed
  values (it recovers the context from the first argument). Same contract.

`compile` is host-side and deterministic: all parties compile the identical
program, then replay it in lockstep. (A `Float_T` body inlines its
`fp<W>_*.empbc` gates into the recording, so a recorded float circuit is
semantically — not necessarily gate-for-gate — equal to the standalone builtin.)

## The compiled circuit object

`compile` returns a `Circuit<RetV, ArgVs...>` wrapping a
`circuit::CircuitArtifact` (the flat `BooleanProgram` + a `CircuitSignature` of
argument widths and the return width). The argument/return template parameters
are value types over `RecordCtx`. The artifact is **private and immutable**;
the constructor validates the program structurally *and* that the signature
matches the declared value widths, so a stale or mis-typed loaded artifact is
rejected at construction rather than silently mis-running. Accessors:
`circ.program()`, `circ.signature()`.

No analyses are baked into the circuit — gate counts, liveness, and the AND-depth
schedule are free functions over the program (`ir/passes.h`), computed when wanted.

## What's inside (internals)

- `circuits/typed.h` — the typed values `Bit_T`/`BitVec_T`/`UInt_T`/`Int_T`/`Float_T<Ctx>`
  (each carries `width()`/`clear_t`/`encode`/`decode`/`rebind<Ctx>` inline) plus
  the bare-wire arithmetic kernels in `emp::kernel`.
- `circuits/value_traits.h` — `value_traits<T>`: the uniform metadata accessor
  (width, clear codec, `rebind<Ctx>`) over a circuit value's own static members.
- `circuits/frontend/rec.h` — `rec::Bit`/`rec::BitVec<N>`/`rec::UInt<N>`/`rec::Int<N>`/`rec::Float<W>`,
  the value types over `RecordCtx` used as `compile` arguments.
- `ir/wire_value.h` — the generic `WireBundle` concept (the structural value
  contract) and `WireValue` (`WireBundle` + the clear codec).
- `ir/context/context.h` — a convenience umbrella re-exporting the
  `BooleanContext` concept and the contexts `ClearCtx` (plaintext) and `RecordCtx`
  (records a `BooleanProgram`), plus the `CountCtx` / `DigestCtx` analysis helpers;
  the value-return replay it re-exports — `execute_program(ctx, prog, inputs, ws)`
  and `ProgramWorkspace` — is defined in `ir/execute.h`.
- `ir/artifact.h` — `CircuitArtifact` (program + signature) +
  `validate_artifact`.
- `circuits/frontend/circuit_fn.h` — the `RecordValue` concept (refining `WireBundle`),
  `circuit_fn_traits` / `circuit_contract`, `Circuit<RetV,ArgVs...>`, `compile`,
  `run`.
- `ir/passes.h` — analyses over the IR (`count_pass`, `liveness_pass`,
  `schedule_pass`, `layout_pass`) as free functions.

## Adding a new backend

Define a type satisfying the `BooleanContext` concept (a `std::semiregular` `Wire`
plus value-return `public_bit`/`and_gate`/`xor_gate`/`not_gate`). Every compiled
circuit then replays on it via `run(ctx, circ, …)` with no frontend changes — the
generic replay walks the gate list issuing the context's gate ops. A
round-sensitive protocol (e.g. GMW) gets efficiency by moving the program into
`ScheduledProgram` and consuming its AND-depth schedule with a
`BulkBooleanContext` + `scheduled_execute_program`, not the scalar replay.

## Tests

- `test/circuits/test_circuit_fn.cpp` — compile-once / run-on-any-context on
  `ClearCtx` (incl. both body forms, a nullary circuit, `.constant()`, fp32),
  plus the size-optimal 31-AND adder and deterministic recording.
- `test/circuits/circuit_fn_contract_probes.cpp` — the contract's positive case +
  the negative cases that must fail to compile with the expected message.
- `emp-sh2pc/test/test_circuit_fn_sh2pc.cpp` — the same compiled `Circuit` run
  two-party over the garbled `SH2PCCtx` (uint32 + fp32).
