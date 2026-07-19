# Changelog

Notable changes to emp-tool. Versions are the git tags. CMake package
metadata is numeric (see the README's version guidance).

## v1.0.0-alpha.1

First tagged alpha of the 1.0 development line. The 0.3.x line
(tag `0.3.0` / branch `v0.3.x`) is maintained separately and receives
backported fixes.

### Stability

- Pre-1.0 API: headers and names may change between alpha tags and
  before the final `1.0.0`. Pin to a specific tag.
- CMake package metadata is numeric `1.0.0` (`project(VERSION)` cannot
  carry a prerelease suffix); the alpha status lives in the git tag.

### Added

- Three-layer library layout: `runtime/` (block/AES/PRG/hash/GF(2^128),
  EC, IO), `ir/` (Boolean-circuit IR, sessions, composition), `circuits/`
  (typed value layer + frontend).
- Typed circuit values (`Bit_T` / `BitVec_T` / `UInt_T` / `Int_T` /
  `Float_T` over a `BooleanContext`) with a compile-once / run-on-any-
  context frontend.
- Persisted circuit assets (`.empbc`) and `empbc-tool`
  (`inspect` / `compare` / `manifest` / `check-manifest`); IEEE
  floating-point and hash circuit assets with per-asset manifests.
- Circuit composition (`ComposeCtx` / `ComposePlan`) — experimental.
- Selectable hash backend (SHA-256 default, BLAKE3) and Fiat–Shamir
  transcript hash (`EMP_FS_HASH`), propagated to consumers as a
  build-time ABI axis (both parties must agree).
- TLS 1.3 IO channel (`TLSIO`) alongside `NetIO`; sibling channels via
  `NetIO::make_sibling`.
- Deterministic test mode (`EMP_TEST_MODE`) for wire-byte equivalence.
- SIMD kernels: SoftSpoken butterfly, six-tier bit-transpose.

### Changed

- Every failure is fail-stop through `error()`; the public surface
  raises no exceptions and is `-fno-exceptions`-compatible (enforced by
  `test_no_exceptions`).
- Requires OpenSSL ≥ 3.0 and CMake ≥ 3.25.

### Security

See the README's Security section: this is research software with no
independent audit; `TLSIO` verifies the peer's certificate chain against
the configured CA only (no identity binding — use a deployment-private
CA); there is no systematic constant-time guarantee.

### Known limitations

Tracked in [docs/roadmap.md](docs/roadmap.md):

- Circuit-asset lookup uses the absolute install path baked at configure
  time, so a prefix moved after install does not resolve assets (override
  with `EMP_CIRCUIT_DIR`).
- SIMD ISA is selected at compile time; a `-march=native` binary can
  fault (SIGILL) on a CPU without those instructions — build portable
  (`EMP_TOOL_NATIVE_ARCH=OFF`) or match `-march` to the target.
