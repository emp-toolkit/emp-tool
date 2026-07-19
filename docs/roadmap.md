# Roadmap

Tracked, not-yet-implemented work. Each entry states the current
behavior, the gap, the proposed design, and the acceptance bar. Nothing
here blocks a first alpha; these are improvements on top of shipping
behavior.

## Circuit-asset reproducibility and semantic verification

**Current.** The floating-point and hash `.empbc` assets
(`emp-tool/ir/files/`) ship as opaque compiled circuits. Integrity is
covered: `emp-tool/ir/files/manifests/` records a SHA-256, I/O widths,
gate/AND counts, and AND-depth per asset, `empbc-tool check-manifest`
verifies the shipped files against it, and CI checks the manifest set
byte-for-byte. What the tree does not ship is a **reproducible
generation harness** — the assets were produced by an external
SoftFloat-derived / CBMC-GC-era pipeline that is not checked in.

**Gap.** Two things the manifests cannot provide. First, *provenance*: a
consumer cannot regenerate an asset from source to confirm it is what the
pipeline produced — the manifest attests to the bytes we shipped, not to
their derivation. Second, *semantic* verification: the manifest fixes the
gate count and hash, but nothing in-repo independently checks that
`fp32_div.empbc` computes IEEE division. `test_float` exercises the
assets behaviorally (bit-exact for the correctly-rounded ops, tolerance
for the iterative ones — see
[floating_point_circuits.md](floating_point_circuits.md)), which is real
coverage, but it is not a from-source regeneration or a formal
equivalence check.

**Proposed design.** Two independent, separately-shippable steps, cheaper
one first:

- **Oracle-vector verification (cheaper, in-CI).** Check in per-asset
  input/expected-output vectors — exact vectors for the
  correctly-rounded ops, reference values for the iterative ones — plus
  a runner that evaluates each shipped `.empbc` on a plaintext context
  and checks the outputs. This verifies the shipped assets compute the
  intended functions without regenerating them, and turns
  `floating_point_circuits.md`'s AND-count and semantics tables into
  something CI enforces rather than prose.
- **Pinned generation harness (fuller, out-of-CI-hot-path).** Check in
  the generator sources and a pinned toolchain (or a container
  descriptor) that reproduces each asset from source, with `empbc-tool
  compare` confirming a regenerated asset matches the shipped one. This
  is the provenance story; it is heavier (external solver/toolchain) and
  need not run on every CI leg.

**Acceptance.**
- Every shipped `.empbc` is checked, on a plaintext context, against
  checked-in oracle vectors for a representative input set (correctly-
  rounded ops bit-exact; iterative ops within the documented tolerance).
- The AND-count / semantics tables in `floating_point_circuits.md` are
  regenerated from, and consistency-checked against, the assets rather
  than hand-maintained.
- (Fuller step) a documented, pinned procedure regenerates each asset
  from source and `empbc-tool compare` reports byte-identical output.

## Runtime ISA dispatch

**Current.** SIMD kernels — AES, CLMUL, and the six-tier bit-transpose
(`runtime/core/simd_tier.h`, `runtime/core/transpose.hpp`) — are selected
at compile time from the build's `-march`. A `-march=native` build embeds
whatever the build host supports (VAES / GFNI / AVX-512 / NEON).

**Gap.** Such a binary executes those instructions unconditionally, so
running it on a CPU without them is an illegal-instruction crash (SIGILL)
with no fallback. The current mitigation is `EMP_TOOL_NATIVE_ARCH=OFF`,
which builds a portable binary pinned to the baseline ISA (x86-64 +
AES-NI/PCLMUL/SSE4.2, or armv8-a+crypto).

**Proposed design — hybrid, not blanket runtime dispatch.** Runtime
feature detection has a real cost: an indirect call per kernel invocation
that also blocks inlining of the hottest block-level kernels, which can
exceed the ISA speedup it buys. So the two build modes stay distinct:

- `EMP_TOOL_NATIVE_ARCH=ON` (deployment builds compiled on the target
  machine) keeps compile-time selection — zero dispatch overhead, peak
  speed. Unchanged.
- The portable path gains runtime dispatch: detect features once at
  startup (x86 `CPUID`, ARM `getauxval`/`HWCAP`), resolve one function
  pointer per kernel family, and branch to the best supported kernel.
  Resolution is once per process, never per operation; the hottest
  inlined kernels keep a compile-time baseline fast path so dispatch
  only gates the wider-ISA upgrades.

This makes one portable binary run everywhere and use the fast path where
the CPU allows, without charging the peak-speed builds for it.

**Acceptance.**
- A portable binary built for the baseline ISA uses VAES/GFNI/AVX-512 on
  a capable host and the baseline kernels on an incapable one, with no
  SIGILL on either.
- `EMP_TOOL_NATIVE_ARCH=ON` throughput is unchanged (no dispatch on the
  hot path).
- Each dispatched kernel family agrees bit-for-bit across tiers (the
  existing transpose/AES parity tests, extended to run per available
  tier at runtime).

## Measured kernel selection (within an ISA tier)

**Current.** Within one ISA tier, each kernel family ships a single
implementation. But the best implementation for a given shape can depend
on the microarchitecture, not just the ISA. Measured example: at the
AVX2 tier, the 128×N bit-transpose at the cache-resident shapes N=8192
and N=16384 runs ~12% faster with a single-lane *blocked* kernel than
with the wide x2-unpack kernel on an Intel Xeon 6 (Granite Rapids), but
~14% *slower* with the blocked kernel on an AMD EPYC 9R45 (Zen 5). A
`ncols==8192||16384 → blocked` special-case was tried and reverted for
exactly this reason: it helps one vendor and regresses the other.

**Gap.** The difference is a uarch/register-file/cache-behavior effect,
so a vendor `if` (Intel vs AMD) is a crude proxy that overfits the two
chips measured and can be wrong on an untested one (Zen 3/4, older
Intel). There is no principled per-shape kernel choice today.

**Proposed design.** Pick the kernel by *measurement*, not by a
hand-written CPU table — the same approach as emp-ot's per-build-directory
auto-tuner (microbenchmark the candidate kernels on the build/first-run
host, cache the winner per shape, select at runtime from the cached
result). This generalizes to every microarchitecture with no guessing and
composes with the runtime ISA dispatch above (measure only within the
tier the host actually runs). The candidates must be output-identical
(the transpose kernels already are), so selection can never change
results — only speed.

**Acceptance.**
- The transpose (and any other multi-kernel family) selects, per shape,
  the fastest measured kernel on the build/run host, recovering the
  ~12% Intel win without the ~14% AMD regression.
- Selection is output-invariant (a parity test proves every candidate
  agrees bit-for-bit) and adds no per-element cost (resolved once, cached).

## Embedder fatal-error observability hook

**Current.** Every failure routes through `error()`
(`runtime/core/error.h`): print the message with its call site, then
`std::_Exit(1)`. This is correct for a standalone two-party protocol
process — a half-garbled circuit or half-consumed OT batch cannot be
safely resumed, and `-fno-exceptions` forbids unwinding.

**Gap.** An embedder running emp-tool as a library inside a longer-lived
process gets no chance to observe *why* the process is dying — no
structured log line, telemetry event, or diagnostic flush — before the
immediate `_Exit`.

**Proposed design — observability, not recovery.** A single process-global
hook, invoked immediately before `_Exit`:

```cpp
// runtime/core/error.h
using fatal_observer_t = void(*)(const char* msg, const char* file, int line);
void set_fatal_observer(fatal_observer_t observer);   // default: none
```

`error()` calls the observer (if installed) and then `_Exit(1)`
regardless of what the observer does. The contract is explicit that this
is for *recording* a fatal event, not surviving it: recovery is not
offered because the protocol state is unsafe to continue and the
no-exceptions contract rules out unwinding. An embedder that needs
per-session isolation runs each session in its own process; the observer
lets the parent capture the cause of a child's exit.

**Acceptance.**
- With no observer set, behavior is byte-for-byte the current
  fail-stop (a test asserts the default path is unchanged).
- With an observer set, it receives the message and call site before the
  process exits, and the process still exits 1 (the observer cannot veto
  termination).
- The hot path is untouched: the hook is read only inside `error()`,
  which is already the cold, `[[noreturn]]` failure path.
