# Composition: inline, replay, and reuse of circuits

How to combine pre-built circuits without paying to materialize the combination.
This extends [frontend.md](frontend.md) (which covers writing a circuit body and
compiling/replaying it); read that first. Everything here is about *one* verb —
`run` — and the difference between **copying** a circuit in and **referencing** it.

> Status: this documents a programming pattern that is implemented but still being
> refined. The composition/reuse path (`run_compose`) currently lowers only on the
> emp-ag2pc backend; the mechanism (`ComposeCtx`, `ComposePlan`) is in emp-tool.

---

## 0. Mental model in three sentences

A compiled circuit is an artifact you **replay**; a raw kernel function is logic you
**build fresh** each call. `run(ctx, unit, args)` replays a compiled unit — *inline*
(emit/compute its gates here) on a normal context, or *by reference* (record one
shared instance) on a `ComposeCtx`. You opt into "by reference" by putting the
repetitive logic in `run_compose`, which turns each `run` call from "inline a copy"
into "reuse the one compiled copy" — the only way to avoid materializing N copies.

---

## 1. The cast

| you write | what it is |
|---|---|
| `template<BooleanContext Ctx> auto f(Ctx&, …)` | a **kernel** — logic, rebuilt every time it's called |
| `compile<ArgVs…>(body)` | freeze a body into a **`Circuit`** (a context-free artifact + signature) |
| `compile_linear<ArgVs…>(body)` | same, but compacted to `WireReuse::Linear` — the form a multi-pass backend (ag2pc) can replay cheaply; use this for a **unit** you intend to compose |
| `run(ctx, circuit, args…)` | **replay** a compiled circuit on `ctx` |
| `run_compose(body, args…)` (session) | run a body whose `run` calls are recorded **by reference** (not inlined) and replayed on the fly |

A direct call `f(ctx, args)` and `run(ctx, compile(f), args)` produce the *same gates*
in inline mode — so if all you want is inlining and you have the source `f`, just call
it. `run` earns its keep for two things a raw call cannot do: replay a circuit you only
have as a **compiled artifact** (e.g. a loaded `.empbc`), and be **referenced** (reused).

See [circuit_fn.h](../emp-tool/circuits/frontend/circuit_fn.h) (`run`, `compile`,
`compile_linear`) and [compose.h](../emp-tool/circuits/frontend/compose.h) (`compose`).

---

## 2. The one rule: inline vs reuse is chosen by context

`run(ctx, unit, args)` branches on the *type* of `ctx`:

- `ctx` is a **`ComposeCtx`** → record one **opaque instance** (a reference to `unit`
  plus its wiring; the unit's gates are NOT walked). This is **reuse**.
- `ctx` is **anything else** (`ClearCtx`, a backend pass, `RecordCtx`, …) → **inline**:
  replay the unit's gates right here.

You never choose at the call site — the *entry point* sets the context:

- `sess.run_compose(body, …)` records `body` once over a `ComposeCtx` → `run` is reuse.
- `sess.run(body, …)` (live), `sess.run(circuit)`, plaintext, `compile(body)` → `run`
  inlines.

So **reuse happens exactly inside `run_compose`, and only there**; everywhere else
`run` inlines. "Reuse when you can, inline when you must" is automatic.

---

## 3. Why both modes exist

Inline is not a legacy fallback — it is the only thing that makes sense outside a
`run_compose` body:

- **Plaintext / value contexts** compute as they go; there is no later "flatten" step,
  so `run` must inline to produce results. (This is the free correctness oracle: the
  same composition body runs on `ClearCtx` and just works.)
- **Building a self-contained circuit** (`compile`, or feeding a backend that only eats
  a flat gate stream) needs the unit's gates inlined. Units themselves are *created* by
  inlining.
- **Even when reuse is possible** it isn't always better: a one-shot or tiny call is
  cheaper inlined, and inlining lets optimizations (DCE, constant-folding) cross the
  unit boundary, which an opaque instance deliberately blocks.

Reuse exists for one reason: **don't materialize repetition.** Calling the same unit N
times inline copies its gates N times; reuse keeps N references to one copy.

---

## 4. When it pays off, and when it doesn't

`run_compose` wins when the repeated/structured work lives in **pre-compiled units**
called via `run`, and the units dominate:

```cpp
auto sha = compile_linear<rec::UInt<256>, rec::UInt<512>>(sha_compress);  // ONCE

// chain (hash chain): N references to one sha, no N-copy materialization
sess.run_compose([&](auto& ctx, auto s, auto... blk){
    ((s = run(ctx, sha, s, blk)), ...); return s; }, init, b0, b1 /*…*/);

// SIMD / batch: N independent instances (schematic — p.key/p.msg and the free
// concat() stand in for real arg-splitting / member .concat())
auto aes = compile_linear<rec::UInt<128>, rec::UInt<128>>(aes_enc);
sess.run_compose([&](auto& ctx, auto... p){
    return concat(run(ctx, aes, p.key, p.msg)...); }, p0, p1 /*…*/);

// tree, reduction, heterogeneous (sha then aes with glue) — all the same shape
```

It does **not** help (and you should use a plain path) when:

- **No reusable units** — a body of direct ops (`a+b`, `a^b`) has nothing to reference;
  under `run_compose` those become "glue" gates and are materialized like normal.
- **A single circuit, once** — replay it with `sess.run(circuit)` on a `compile_linear`
  circuit; there's no repetition to share, and `run_compose` only adds plan overhead.
- **Live body replay** `sess.run(body)` with `run` inside — there `ctx` is a backend
  pass, so `run` inlines (per replay). To get reuse you must switch to `run_compose`.
- **Plaintext / non-ag2pc backends** — `run` inlines (correct, materialized).

The win scales with how much work is in reused units versus glue, and with how many
instances amortize compiling each unit once.

---

## 5. `run` inside `run` is one level deep

Reuse is not recursive. A `run(ctx, unit)` reuses `unit` as an opaque instance, but the
inner structure of `unit` was fixed when `unit` was compiled:

- A unit whose *body* called `run(ctx, subunit)` had that subunit **inlined at compile
  time** — the unit is flat, and composing it later is **one** opaque instance with the
  subunit baked in.
- Only the `run` calls **directly in the `run_compose` body** are the reuse boundary.

Multi-level (a unit that is itself a live composition) is not supported yet — for now,
inline the inner level into each unit and compose the outer level.

---

## 6. EMP-AG lowering

On emp-ag2pc, `sess.run_compose(body, …)`:

1. records `body` once over a `ComposeCtx` → a `ComposePlan` (O(#glue gates + #instances));
2. lowers it: each instance gets a disjoint slot block, and each unit's gate
   stream is replayed without constructing a flattened N-copy program.

Requirements/notes:

- Dense (`WireReuse::None`) and `compile_linear` units are both safe on the current
  multi-pass backend. Linear units use fewer per-instance slots and are therefore
  the recommended form. `WireReuse::Full` is rejected because it may recycle an
  AND-output slot whose state survives across passes. See
  [transform.h](../emp-tool/ir/transform.h) for `make_compact` and
  [program.h](../emp-tool/ir/program.h) for the three reuse levels.
- `ComposeCtx::finish` checks plan-level wiring. Raw-plan consumers validate the
  structure and every distinct unit before indexing it.
- `run_tiled(unit, repeats, state)` is one-line sugar over `run_compose` for the common
  state→state chain.

The plan is also independently materializable (`flatten_compose` in
[ir/context/compose.h](../emp-tool/ir/context/compose.h)) for a backend with no
on-the-fly flattener.

---

## 7. Deferred / open

- Multi-level (nested) composition — compositions as units of larger compositions.
- A batch/SIMD sugar (independent inputs, all outputs) over `run_compose`.
- Lowering the compose path on backends other than ag2pc.

---

## TL;DR

`run` = replay a compiled circuit. Replay-now (inline) on a normal context; record a
replay-by-reference (reuse) on a `ComposeCtx`. Put the repetitive logic in
`run_compose` and its `run` calls stop copying the unit and start sharing it — the only
way to run a circuit many times without materializing the repetition.
