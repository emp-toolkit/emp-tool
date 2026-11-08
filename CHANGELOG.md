# Changelog

Notable changes to emp-tool. Versions are the git tags. CMake package
metadata is numeric (see the README's version guidance).

## Unreleased

### Security

- `MITCCRH`: the bucketed key reuse (`ReuseShift > 0`, default 3) now
  carries the gate id into the cipher input,
  `H(x, i) = y ⊕ E_{S ⊕ (i >> ReuseShift)}(y)` with `y = σ(x) ⊕ T(i)`,
  `T(i) = makeBlock(i, 0)`. Previously the gids sharing a bucket key
  hashed a block to the same value, so a wire label fed to two AND gates
  of one bucket (or `AND(a, a)`) gave half-gate rows whose XOR was `0` or
  `Δ`, and the evaluator recovered `Δ`. Affects `v1.0.0-alpha.1`, the
  first release with the bucketed `ReuseShift` hash; the 0.3.x line keys
  every gate separately and is unaffected.
  Every MITCCRH-derived transcript changes; both parties must upgrade
  together, pinned wire-trace baselines must be regenerated, and every
  dependent must be rebuilt (the class layout changed). (Issue #212.)

### Added

- `tcp::SocketOptions` (`runtime/io/tcp_socket.h`): send and receive buffer sizes
  for `NetIO::listen` / `NetIO::connect` (new overloads) and
  `TLSConfig::socket_options`, applied before `listen()` / `connect()` so TCP
  window scaling covers them; accepted sockets inherit them and `make_sibling()`
  passes them on. `SocketOptions::for_bandwidth_and_rtt(bandwidth, rtt)` sizes both
  directions for a path: zero, the operating system's own sizing, up to a 4 MiB
  bandwidth-delay product, and the first power of two above it beyond that. A size
  above the system's limit (`net.core.rmem_max` / `wmem_max` on Linux, 212,992 bytes
  unless raised; `kern.ipc.maxsockbuf` on macOS, 8 MiB) is lowered to the limit when
  that is above 4 MiB and otherwise not made, keeping the operating system's own
  sizing; one warning per process gives the `sysctl` command that allows it.
- `vector_inn_prdt_sum_red_x4(res, a, b, n)` (`crypto/f2k.h`): four
  independent GF(2¹²⁸) inner products in one call, with the operands laid
  out term-major (`a[4l + j]` is term `l` of sum `j`). Each SIMD lane
  carries one sum, so the 512- and 256-bit VPCLMULQDQ tiers fold nothing
  across lanes, and each sum is reduced once at the end, bit-identical to
  `vector_inn_prdt_sum_red` on the de-interleaved operands.

### Changed

- `IOChannel::sync()` is one non-virtual implementation in the base, used by every
  channel: each side sends one zero byte, flushes, and waits for the peer's, so
  neither side has to go first and no channel can turn it into a no-op. Two
  channels adopted from existing sockets (both `is_server == false`) used to
  deadlock in it, each waiting to receive first. It still stays out of the byte
  counters and the Fiat-Shamir transcript and sends the same bytes as before, so
  old and new peers still pair up, and it now reports a nonzero byte as the two
  sides being out of step. NetIO, TLSIO and TraceIO no longer override it; a
  custom channel that overrode `sync()` must drop the override, and dependents must
  be rebuilt, since `IOChannel`'s virtual table changed.
- `NetIO::flush()` stops the process when the stdio `fflush` fails instead of
  ignoring it.
- `TraceIO` rejects a null channel, creates its trace files with mode `0600`,
  flushes them before the wrapped channel, reports trace write and flush failures,
  and records the bytes of `sync()`.
- `EMP_TEST_MODE` turns test mode on only for the exact value `1`; values such
  as `10` or `1x` used to turn it on.
- Test mode stops the process in more of the cases where deterministic streams
  would repeat. Deriving a child lane on lane 0, as `ThreadPool::enqueue` does,
  now claims lane 0 just as drawing a seed does, so a second unscoped thread
  that enqueues pool work must run under its own `test_lane_scope`. Lane 0's
  owner is a per-thread token rather than a hash of `std::thread::id`, so a
  thread started after the owner exits can no longer reuse its id and replay
  its seeds. `test_lane_scope(0)` stops the process; on lane 0's owner it
  restarted lane 0's ordinals and repeated the seeds already drawn.
- `parse_party` and `peer_port` stop the process unless the value is plain
  decimal digits within range (`EMP_PORT` must be in [1, 65535]). They used
  `atoi`, which read `2x` or `+2` as 2 and passed any `EMP_PORT`, such as `0` or
  `70000`, through unchecked.
- `clock_start` / `time_from` use `std::chrono::steady_clock`, which does not
  jump when the system clock is set. On libstdc++, where
  `high_resolution_clock` is `system_clock`, their type changes: code that
  stores `clock_start()` in a `time_point<high_resolution_clock>` must use
  `auto` or `steady_clock::time_point`.
- `ThreadPool(0)` stops the process; such a pool accepted tasks that never ran,
  so their futures never became ready. A worker thread that fails to start no
  longer ends in `std::terminate`.
- `ThreadPool::enqueue` stores its arguments without `std::bind`. They are
  passed as lvalues, as before, or as rvalues when the callable cannot take
  lvalues, so a function taking a move-only argument by value can be enqueued;
  the future's type is the result of that call, which can differ from
  `invoke_result_t<F, Args...>` for a callable overloaded on value category.
  `std::bind`'s special cases are gone: a `std::reference_wrapper` argument is
  no longer unwrapped, so a generic parameter sees the wrapper (a `T&`
  parameter still binds through its conversion), and a nested bind expression
  is passed as is instead of being called.
- `bool_to_int<bool>` no longer compiles; it wrote eight packed bits into a
  `bool`, which may only hold 0 or 1.

### Removed

- `TLSIO::is_server`, which only chose who went first in the old `sync()`.
- `MITCCRH::renew_ks(const block*)` and `MITCCRH::hash_cir_fixed`, the
  explicit-tweak mode in which the caller scheduled the keys and owned
  tweak uniqueness. Every hash now goes through the gid stream, which the
  class keeps distinct per `(S, gid)`; a second tweak dimension is XORed
  into the low qword of a random `S` or laid out as gid gaps with
  `renew_ks(gid)`. A protocol that scheduled its own keys and bucketed on
  explicit tweaks shared one key across gates with nothing in the cipher
  input to tell them apart, the same leak one layer up; it must re-derive
  its tweak layout on the gid stream, on both ends, not rename calls.
- `macro_xstr` / `macro_str`, which `utils.h` defined for every file that
  includes it, although emp-tool does not use them.

### Fixed

- `bench_halfgate` measured nothing: with the hashes inlined into `main`
  and their results unused, the compiler removed them, so the loop timed
  the PRG and key schedules, and the printed rate divided 128M gates by a
  2M-gate loop. The outputs are now folded into a printed sink and the
  count matches the loop.
- `joinNcleanCheat` stopped calling `get()` once a task returned `true`, so it
  could return while later tasks were still running. It now waits for every
  future.
- `error()` flushes `stderr` before exiting, so a diagnostic written to a
  buffered `stderr` is no longer lost.
- A translation unit that included `runtime/core/simd_tier.h` before
  `runtime/core/block.h` compiled `sse_trans` without its AVX2 / AVX-512 / GFNI
  kernels. Linked with translation units that included them in the usual order,
  the program had two definitions of the same inline functions, and the linker
  could pick the slower one for every call that was not inlined.

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
