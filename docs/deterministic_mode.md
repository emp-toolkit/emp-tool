# Deterministic mode — reproducible randomness for wire-byte equivalence

A deterministic-mode build of emp-tool swaps every randomness source in the
toolkit for a deterministic, counter-derived stream. In such a build, the wire
bytes a protocol produces are byte-identical across runs of the same code. Two
complementary headers build the framework:

| Header | Role |
|---|---|
| [`emp-tool/runtime/core/deterministic_mode.h`](../emp-tool/runtime/core/deterministic_mode.h) | The build switch, the party and the lane machinery. `is_deterministic_mode()`, `set_deterministic_party()`, `next_deterministic_seed()`, `reset_deterministic_seeds()`, `deterministic_lane_scope`, `next_deterministic_child_lane()`. |
| [`emp-tool/runtime/io/trace_io.h`](../emp-tool/runtime/io/trace_io.h) | `TraceIO` IOChannel adapter that tees wire bytes to a pair of files alongside delivering them. |

## When to use it

Verifying that an optimization or refactor doesn't change a
protocol's observable wire behavior. Run the protocol once before
the change, once after; diff the two `TraceIO` outputs. If both
are byte-identical, the change is wire-equivalent.

This catches a class of bugs that correctness assertions miss:
two implementations of "the same protocol" can each pass per-OT
output verification while sending the wire bytes in different
orders, which breaks any peer running on the unchanged code.

## What deterministic mode swaps

Two randomness sources in emp-tool reach the wire:

1. **`PRG()` default-construction** — used pervasively for keys,
   Δ sampling, masks, check seeds, and other protocol randomness.
   In deterministic mode, `PRG()` takes a `(party, lane, ordinal)`
   seed from `next_deterministic_seed()` instead of OS entropy
   (`getentropy()`): the party is the one the process plays, the
   lane names the logical unit of work (main thread = lane 0), and
   the ordinal counts constructions within the lane.
2. **`ECGroup::rand_scalar`** — the only call to OpenSSL's
   `BN_priv_rand_range` in emp-tool, used by elliptic-curve
   protocols. In deterministic mode, each call samples uniform in
   `[1, order)` from a fresh default `PRG`, by rejection, instead of
   OpenSSL's internal CSPRNG.

`PRG(const void*, int)` (explicit seed) is unaffected — callers
that pass their own seed already control determinism.

## Turning it on

Deterministic mode is a build option, not a runtime switch:

```bash
cmake -B build-det -DEMP_DETERMINISTIC_MODE=ON
cmake --build build-det -j
```

`-DEMP_DETERMINISTIC_MODE=ON` defines `EMP_DETERMINISTIC_MODE` for emp-tool and,
through its exported target, for every library and program that links it, so a
dependent built against a deterministic-mode emp-tool is a deterministic-mode
build too. `is_deterministic_mode()` is a compile-time constant. A normal build
compiles the deterministic paths out, so no environment variable or call can make
its randomness predictable. Objects built the two ways cannot be linked into one
program: the link fails on an undefined `emp::DeterministicModeAbiMarker` symbol.

A deterministic-mode build prints a prominent warning to `stderr` at its first
deterministic draw, once per process, that default PRG seeds and EC scalars are
predictable. Never deploy such a build or process real secrets with it.

`reset_deterministic_seeds()` rewinds every lane's ordinal and releases lane 0.
Call it between independent test iterations, after joining threads and draining
pool futures, to get reproducible PRG sequences within one process.

The mode was called test mode before; `-DEMP_TEST_MODE=ON` still turns it on,
the build still defines `EMP_TEST_MODE` along with `EMP_DETERMINISTIC_MODE`, and
the old function and type names (`is_test_mode()`, `test_lane_scope`, ...) remain
as deprecated aliases. They will be removed in a later release.

## Parties

The processes of a multi-process run all start from the same `(lane, ordinal)`
schedule, so without more each would draw exactly what every other draws: the same
keys, the same Δ, the same masks. That is not how real randomness behaves, and a
test of a cheat can pass in such a build only because the cheater's values equal
the honest parties'. So every seed also carries the party the process plays:
`set_deterministic_party(p)` fixes it for the process, and `parse_party()`, which
every multi-process test and example calls on its `argv`, calls it. Party 0, the
default, is a process that set none, such as a single-process test; its seeds are
the bare lanes.

Seeds drawn before any party is set are party 0's. Once a process has drawn a seed
as one party, setting another aborts, since it would mix two parties' streams in
one process. Parties that run as threads of one process are told apart by their
lanes instead.

## Multi-threading: lanes

A seed's lane and ordinal make determinism under threads reduce to one
rule: **every spawned worker draws under its own lane, chosen by the
code that created the work** (whose program order is deterministic),
never discovered by the worker itself.

- **`ThreadPool` tasks: automatic.** `enqueue` derives a lane on the
  enqueuing thread and installs it around the task body. Nested
  enqueues derive hierarchically, so they stay deterministic too.
- **Hand-spawned `std::thread`s: one line.** The body's first
  statement installs the creator-chosen lane:

  ```cpp
  std::thread([&, peer]() {
      emp::deterministic_lane_scope guard(0x100 + peer);  // any stable id
      // ... PRG() draws here come from (0x100+peer, 0), (…, 1), ...
  });
  ```

  Manually assigned lane ids must be nonzero and unique among concurrently
  active work. Lane 0 is the unscoped lane: the first unscoped thread to draw
  a seed or derive a child lane owns it until `reset_deterministic_seeds()`.

- **Forgetting is loud.** A second thread drawing from lane 0 or deriving a
  child lane from it would replay deterministic streams byte-for-byte, so
  deterministic mode aborts with a pointer to this document instead.

Lanes make the *randomness* reproducible. Byte-identical *traces*
additionally require that each traced channel has a single writer
lane — see the contract below.

### Cost in production

None: a normal build compiles the deterministic paths out
(`if constexpr (is_deterministic_mode())`).

## Determinism contract

- **Threads need lanes.** Pool tasks get one automatically;
  hand-spawned threads install `deterministic_lane_scope` with a stable
  id (peer index, slice number). Seeds are then reproducible under any
  scheduling; wire-byte identity additionally requires one writer
  lane per channel, since concurrent writers to a single channel
  interleave nondeterministically even with deterministic seeds.
- **Processes need parties.** `parse_party()` sets one; a multi-process
  program that does not use it calls `set_deterministic_party()` itself.
- **A connecting NetIO draws a seed.** Its constructor runs the
  session-root coin flip, which default-constructs a `PRG`, so a NetIO
  built on a hand-spawned thread needs a lane like any other draw
  (`NetIO::SessionRoot::none` draws nothing).
- **`PRG(const void*, int)` is unchanged in deterministic mode.**
  Callers with their own explicit seed sources already control
  determinism; the hook only affects the OS-random default path.
- **External inputs are not deterministic.** Test inputs (choice
  bits, message buffers) come from the caller — the test harness
  must seed them deterministically too.

## Recording a trace

`TraceIO` wraps any `IOChannel*` and writes a copy of every wire
byte to two files: `<prefix>.send` and `<prefix>.recv`. Bytes are
copied before outbound delivery and after inbound delivery, including
the zero bytes of `sync()`, which runs on the same hooks. Trace files are
created with mode `0600`; `TraceIO::flush()` flushes both files before
flushing the wrapped channel.

```cpp
NetIO* under = new NetIO(...);
TraceIO* io  = new TraceIO(under, "before.alice");
// ... use `io` exactly like a NetIO; protocol writes get teed to
// before.alice.send / before.alice.recv ...
delete io;
delete under;
```

Counters: `TraceIO` inherits the `IOChannel::send_counter` /
`recv_counter` book-keeping from the base. The wrapped underlying
channel's counters do NOT increment for traced bytes — protocol
code that observes `io->send_counter` / `io->recv_counter` reads
the wrapping `TraceIO`'s counts, which is correct.

## Verification workflow

For a refactor / optimization where wire-equivalence is required:

```bash
# A deterministic-mode build (see "Turning it on").
cmake -B build-det -DEMP_DETERMINISTIC_MODE=ON && cmake --build build-det -j
# Record the "before" trace (both Alice and Bob).
./run ./build-det/your_protocol_test before
# … apply your refactor / optimization, rebuild build-det …
./run ./build-det/your_protocol_test after

# Each direction of the wire, checked at its sender; both must be empty.
diff before.alice.send after.alice.send
diff before.bob.send   after.bob.send
```

A direction is diffed at its *sender*: the receiver's `.recv` file only
contains bytes the application consumed, so it can miss trailing bytes a
refactor sends that the peer never reads. The two `.send` files together
cover the full wire.

[`test/runtime/test_traceio.cpp`](../test/runtime/test_traceio.cpp) shows
`TraceIO` in use; its `example()` is a starting point for new
wire-equivalence tests.

## What deterministic mode does NOT cover

- **Transcript interleaving across lanes.** Lanes fix the seeds;
  they cannot fix the order in which concurrent lanes' bytes land on
  a *shared* channel. Keep one writer lane per traced channel (the
  per-thread-NetIO pattern).
- **Caller-provided randomness.** A `PRG` constructed with an
  explicit seed bypasses the hook entirely.
- **Other OpenSSL randomness.** Only `BN_priv_rand_range` (via
  `ECGroup::rand_scalar`) is intercepted. Direct `RAND_bytes` calls,
  if any code path adds them, would still pull from OpenSSL's
  CSPRNG. emp-tool makes no such call:
  `grep -rn "RAND_bytes\|rand_range" emp-tool` finds only the
  intercepted `BN_priv_rand_range`.
- **Non-determinism from outside the toolkit.** Wall-clock time,
  PIDs, system-state-dependent paths — none of these affect wire
  bytes, but are flagged for completeness.
