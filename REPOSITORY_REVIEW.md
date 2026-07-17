# Repository review

Date: 2026-07-15

## Executive assessment

emp-tool is a strong architectural foundation with unusually good documentation
and test discipline for a header-heavy cryptographic C++ library. The
`circuits -> ir -> runtime` layering is coherent, the context/session split
avoids global-backend problems, and the IR and `.empbc` validation work is
thoughtful.

The main gap is hardening: several public boundaries still assume trusted
inputs, and some defects are hidden by the current test infrastructure. The
first four findings below should be addressed before the next release.

## Highest-priority findings

### 1. Alignment undefined behavior in transpose code

`emp-tool/runtime/core/transpose.hpp` writes through potentially unaligned
`uint16_t*` pointers. An alignment-enabled UBSan run failed at the generic
transpose store with a misaligned-store error. CI disables alignment
sanitization globally in `.github/workflows/build.yml`, masking the defect.

Recommendation: use `memcpy` or explicit unaligned load/store helpers, then
remove `-fno-sanitize=alignment`.

### 2. The two-party harness can report success when one party fails

`run` backgrounds party 1 but never waits for or aggregates its exit status. A
probe where party 1 exited 1 and party 2 exited 0 produced harness exit status
0. Arguments are also unquoted.

Recommendation: capture both PIDs, wait for both, propagate either failure,
terminate the sibling on failure, install signal traps, and impose a timeout.

### 3. EC deserialization is unsafe for some advertised curves

The README says callers may pass any OpenSSL `NID_*`, while `Point::from_bin`
checks only on-curve and non-infinity. That is sufficient for cofactor-1 P-256,
but not necessarily for cofactor-greater-than-one curves, where points outside
the prime-order subgroup can be accepted.

Recommendation: either restrict `ECGroup` to explicitly supported cofactor-1
curves or verify `[order]P == infinity` during deserialization. Add a
cofactor-greater-than-one negative test.

### 4. Signed length APIs permit negative values and overflow

Examples include `PRG::random_data`, `IOChannel::send_data`, and `send_block`
multiplication. Negative values can become enormous `size_t` arguments or
corrupt transcript counters. Separately, `getBit` has shift UB for invalid
indexes; a targeted UBSan probe confirmed the negative-shift case.

Recommendation: keep the repository's `int64_t` length convention, reject
negative values before counters, arithmetic, pointer operations, hashing, or
conversion to unsigned library sizes, guard count-to-byte multiplication, and
make bit-index validation active in Release builds.

## Correctness and API design

- Release and sanitizer configurations compile away meaningful checks because
  several tests use plain `assert`, including `test_netio.cpp`,
  `test_tlsio.cpp`, and `test_ro.cpp`. Debug CI exercises them, but
  Release/RelWithDebInfo legs do not. Replace these with always-on test
  assertions and prohibit runtime `assert` in tests.

- `ComposePlan` is public and mutable, while `ComposeInstance` stores
  non-owning program pointers through `ComposeCtx::call_unit`. This creates
  lifetime hazards and allows malformed plans to reach flattening/execution.
  Prefer immutable/frozen plans with owned or interned
  `shared_ptr<const CircuitArtifact>` identities.

- `ScheduledPlan` is public and mutable. Make scheduled buckets opaque and bind
  them to an immutable program digest or artifact identity.

- Enforce `WireReuse` requirements at composition/execution boundaries. The
  current validator can prove executability but cannot establish that an
  externally supplied "Linear" artifact truly follows the intended production
  discipline. For untrusted compact programs, uncompact and canonically
  recompact them.

- Establish explicit `validate_canonical()` and `canonicalize()` APIs. Several
  consumers currently risk independently deriving what canonical IR means.

- The documented no-exceptions policy conflicts with the installed public
  `emp-tool/third_party/ThreadPool.h`, which explicitly throws. Either
  encapsulate/replace it or qualify the policy in `docs/api_conventions.md`.

- Complete OpenSSL error handling. Several scalar arithmetic and
  point-construction calls in `runtime/crypto/ec.cpp` ignore return values.

## Networking and security

- TLS validates the certificate chain but does not perform hostname or explicit
  peer-identity verification. Add SNI/expected identity parameters and use the
  appropriate OpenSSL hostname-verification APIs.

- Accept, handshake, and read operations have no deadlines and can block
  indefinitely. Add configurable connection and operation deadlines plus
  cancellation.

- NetIO remains exposed to `SIGPIPE`; constructing TLSIO happens to disable it
  globally, which is surprising process-wide behavior. Use per-send suppression
  where available or establish one documented process policy.

- `tcp_socket.h` accepts only IPv4 literals through `inet_addr`. Move to
  `getaddrinfo` for DNS and IPv6, and check all socket-option/accept return
  values.

- `make_sibling()` uses timing/port-reuse coordination. Replace the sleep
  heuristic with a persistent listener or a negotiated secondary port and
  channel token.

- `EMP_TEST_MODE` is valuable for deterministic transcript testing, but
  production activation should be structurally difficult. Prefer a test-only
  build option or an explicit capability injected into the session rather than
  a broadly available environment switch.

## Build, packaging, and portability

- `EMP_HASH_BACKEND` is documented as `sha256|blake3`, but CMake does not
  validate it. `-DEMP_HASH_BACKEND=bogus` configured successfully and silently
  selected SHA-256. Apply validation equivalent to `EMP_FS_HASH`.

- The main build requires OpenSSL 3.0, but the exported package only calls
  `find_dependency(OpenSSL)`. Preserve the same 3.0 minimum for consumers.

- `EMP_TOOL_NATIVE_ARCH` defaults to `ON`. For an installed reusable library,
  portable should be the default; benchmarks and local optimized builds can opt
  into native ISA. Longer term, consider runtime CPUID/HWCAP dispatch.

- CI should add Linux Clang, shared-library builds, install-tree consumer tests,
  explicit BLAKE3/Fiat-Shamir hash combinations, and at least one native-ISA
  job.

- The project is effectively POSIX-only. State the supported Linux/macOS
  platforms explicitly and fail early on unsupported systems.

- `CMakeLists.txt` is stored with executable mode `100755`; normalize it to
  `100644`.

- Ensure installed distributions include all vendored dependency notices and
  licenses, not only public headers.

## Tests and generated assets

- Add fuzz targets for `.empbc` loading, validation, uncompaction, composition
  flattening, point deserialization, IO framing, and bit-packing. Parsers should
  also enforce configurable resource limits before allocating from declared
  counts.

- Add CTest-level timeouts. A deadlocked server currently relies on the much
  larger workflow timeout.

- TLS tests and benchmarks share fixed `/tmp/emp_tlsio_test_*` files. Parallel
  builds or stale files can interfere. Use a unique per-run temporary
  directory, atomic readiness, and cleanup.

- Add direct coverage for `MITCCRH::hash_cir_fixed`; current tests exercise
  neighboring functionality but not that specific API.

- Add independent transpose reference vectors. A self-roundtrip mostly proves
  that encode and decode agree with each other, not that either matches the
  intended layout.

- The 63 committed `.empbc` assets are critical supply-chain inputs, but their
  README acknowledges there is no complete reproducible generation harness.
  Add a pinned generator/compiler environment; exact source revision and
  command per asset; a manifest containing SHA-256, widths, gates, and
  wire-reuse mode; a CI regeneration/check target; embedded expected digests
  for shipped builtins; and an explicit unsafe/development flag for
  `EMP_CIRCUIT_DIR` overrides.

## Benchmarks, documentation, and governance

- Extract the duplicated `run_for` benchmark implementation into one harness.
  Report median/variance, actual CPU metadata, machine-readable JSON/CSV, and
  baseline comparisons instead of assumed "cycles @3GHz".

- Initialize network benchmark buffers; `bench_netio.cpp` allocates data
  without deterministic initialization.

- Add IR/frontend benchmarks: validation, load/parse, compact/uncompact,
  scheduling, compose/flatten, circuit cold-start, and peak memory.

- Documentation is a genuine strength, but audit documents mix current and
  already-resolved findings. Convert them into a maintained issue register with
  status, owner, affected version, and regression-test reference.

- Add `SECURITY.md` first, followed by `CONTRIBUTING.md`, changelog/release
  policy, CODEOWNERS, issue/PR templates, dependency-update automation,
  ABI/protocol compatibility notes, SBOM, and signed release provenance.

## Recommended order

1. Fix transpose UB, harness exit handling, EC curve/subgroup safety, and
   length/index validation.
2. Convert test assertions, add timeouts and isolated TLS fixtures, then enable
   full UBSan alignment checks.
3. Harden TLS/NetIO and make composition/schedule artifacts immutable.
4. Make generated assets reproducible and authenticated.
5. Expand CI, fuzzing, and install-consumer coverage.
6. Improve release governance, benchmark infrastructure, and runtime ISA
   dispatch.

## Verification performed during review

- Fresh Release, Debug, and BLAKE3 builds succeeded.
- All three standard configurations passed 47/47 tests.
- Release installation succeeded.
- A separate alignment-enabled UBSan probe failed at the confirmed transpose
  issue.
- A targeted `run` probe confirmed that a background-party failure can be
  hidden.
- A targeted CMake probe confirmed that an invalid `EMP_HASH_BACKEND` is
  accepted.
- A targeted UBSan probe confirmed invalid `getBit` indexes can cause shift UB.

