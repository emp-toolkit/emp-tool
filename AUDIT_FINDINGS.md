# emp-tool — audit findings

Surfaced by refreshing the implementation audit in [`audit-report/`](audit-report/)
against HEAD `b1f4efd` (the report was last reconciled at `551edc7`; seven
commits landed since). Full detail and anchors live in each page's *Auditor
risk register*. This file is the action-facing subset — the new/changed
risks the seven commits introduced.

Anchors are repo-relative. See also [`verdicts.txt`](verdicts.txt) (the
2026-06-10 design review) for architecture-level verdicts.

## Test-coverage gaps (highest value)

### T1 · BLAKE3 backend has no in-tree known-answer test
`crypto/hash.h:23-54`; `test/.../test_hash.cpp`; `CMakeLists.txt:149-180` · commit `b1f4efd`

`test_hash.cpp` pins SHA-256 answers against the bare `Hash` alias with **zero
conditional compilation**, so (a) a `-DEMP_HASH_BACKEND=blake3` default build
*fails* `test_hash`, and (b) there is no BLAKE3 known-answer coverage anywhere.
The optional backend ships untested. Add a backend-parameterized KAT.

### T2 · `MITCCRH::hash_cir_fixed` has zero test coverage
`crypto/mitccrh.h:119-133` · commit `4af78dd`

Grep-verified: only its definition and comment exist — no caller under
`test/`. The explicit-tweak schedule-reuse method must pair only with
`renew_ks(tweaks)`; mode-mixing with the counter-tweak schedule is unchecked
(`mitccrh.h:86-94`). Add a test that exercises it against the counter path.

## New invariant / ABI surfaces

### R1 · WireReuse relaxes the IR single-writer invariant
`ir/validate.h:50-93`; `ir/transform.h`; `ir/files/empbc.h:127-132`; `ir/passes.h:31,73,94,134`; `ir/schedule.h:63` · commit `67642c2`

`BooleanProgram` is no longer solely dense/SSA — it carries a `WireReuse` mode
(None/Linear/Full). `validate_program`'s reuse branch proves **executability
only, not equivalence**: it cannot verify Linear's And-pinning (that contract
lives in the producer, `transform.h`). Mitigations in place: `.empbc` flags
bits 0-1 fail closed; `require_dense` guards convert a silent reuse-program
miscompute into a fatal rejection at the dense passes. Auditor takeaway: trust
in a reused-wire program rests on the *producer*, not the validator.

### R2 · Hash backend is a build-time ABI axis
`crypto/hash.h:23-54`; `crypto/ec.cpp:504-509`; `CMakeLists.txt:149-180` · commit `b1f4efd`

`emp::Hash` is now `HashT<EMP_HASH_DEFAULT>` with the backend chosen at
configure time and **PUBLIC-propagated** to consumers. A `HashAbiMarker`
link-time guard prevents mixing translation units built against different
defaults (ODR safety). Note: selecting `blake3` is a **protocol-wide
transcript change** — any two peers must agree on the backend.

### R3 · `ComposePlan` holds non-owning program pointers
`ir/context/compose.h:28-32` · commit `67642c2`

`ComposePlan` stores `const BooleanProgram*` references to its unit programs;
lifetime is the caller's obligation — a plan outliving its units dangles.

## ISA dispatch

### D1 · Six-tier bit-transpose dispatch is compile-time only
`core/transpose.hpp:748-772`; `core/simd_tier.h:20-98` · commits `c57538b`, `0b07d67`, `d836db5`

The transpose moved out of `block.hpp` into `core/transpose.hpp` and grew to a
six-kernel family (GFNI512 / GFNI-VEX-256 / AVX512BW / AVX2 / SSE2 + native
NEON) selected at build time with **no runtime CPUID** — same SIGILL-on-
wrong-host hazard as the existing `simd_tier` AES/CLMUL dispatch, now over a
wider ISA surface. The six kernels are anchored only to the generic SSE2
`sse_trans` parity test, which is itself round-trip-tested only.

---

*The 2026-06-10 design review (`verdicts.txt`) remains valid at the
architecture level; this file only records what the post-`551edc7` commits
added to the risk surface.*
