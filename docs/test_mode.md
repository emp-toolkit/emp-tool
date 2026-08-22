# Test mode — deterministic randomness for wire-byte equivalence

emp-tool exposes a "test mode" flag that swaps every randomness
source in the toolkit for a deterministic, counter-derived stream.
With test mode on, the wire bytes a protocol produces are byte-
identical across runs of the same code. Two complementary headers
build the framework:

| Header | Role |
|---|---|
| [`emp-tool/runtime/core/test_mode.h`](../emp-tool/runtime/core/test_mode.h) | The toggle and the lane machinery. `is_test_mode()`, `set_test_mode(bool)`, `next_test_seed()`, `reset_test_seed_counter()`, `test_lane_scope`, `next_test_child_lane()`. |
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

## What test mode swaps

Two randomness sources in emp-tool reach the wire:

1. **`PRG()` default-construction** — used pervasively by base OTs,
   PPRF roots, Δ sampling, malicious-mode consistency check seeds,
   etc. In test mode, `PRG()` pulls a deterministic `(lane, ordinal)`
   seed from `next_test_seed()` instead of OS entropy
   (`/dev/urandom`): the lane names the logical unit of
   work (main thread = lane 0), the ordinal counts constructions
   within the lane.
2. **`ECGroup::rand_scalar`** — the only call to OpenSSL's
   `BN_rand_range` in the toolkit. Used by P-256 base OTs (PVW,
   CSW, NP, CO). In test mode, it samples uniform in `[0, order)`
   via emp::PRG-driven rejection instead of OpenSSL's internal
   CSPRNG.

`PRG(const void*, int)` (explicit seed) is unaffected — callers
that pass their own seed already control determinism.

## Toggle mechanics

Two ways to enable, equivalent in effect:

```bash
EMP_TEST_MODE=1 ./build/your_test
```

```cpp
emp::set_test_mode(true);  // before any PRG() default-construction
```

The env var is read once at first call to `is_test_mode()` and
cached. `set_test_mode()` overrides it programmatically.

The first activation by either mechanism prints a prominent warning to
`stderr`, once per process, that default PRG seeds and EC scalar randomness are
deterministic and insecure. The warning happens at activation rather than on
each random draw, so it adds no work to the randomness hot path. Never process
real secrets in a process running in test mode.

`reset_test_seed_counter()` rewinds every lane's ordinal and
releases lane 0 — call it between independent test iterations to
get reproducible PRG sequences within one process.

## Multi-threading: lanes

A seed is the pair `(lane, ordinal)`, so determinism under threads
reduces to one rule: **every spawned worker draws under its own
lane, chosen by the code that created the work** (whose program
order is deterministic), never discovered by the worker itself.

- **`ThreadPool` tasks: automatic.** `enqueue` derives a lane on the
  enqueuing thread and installs it around the task body. Nested
  enqueues derive hierarchically, so they stay deterministic too.
- **Hand-spawned `std::thread`s: one line.** The body's first
  statement installs the creator-chosen lane:

  ```cpp
  std::thread([&, peer]() {
      emp::test_lane_scope guard(0x100 + peer);  // any stable id
      // ... PRG() draws here come from (0x100+peer, 0), (…, 1), ...
  });
  ```

- **Forgetting is loud.** A second thread drawing from lane 0 would
  replay the main thread's streams byte-for-byte — silently wrong —
  so test mode aborts with a pointer to this document instead.

Lanes make the *randomness* reproducible. Byte-identical *traces*
additionally require that each traced channel has a single writer
lane — see the contract below.

### Cost in production

One cached `std::atomic<bool>` load per `PRG()` default-
construction. Branch predicts perfectly; no measurable overhead
in production paths.

## Determinism contract

- **Threads need lanes.** Pool tasks get one automatically;
  hand-spawned threads install `test_lane_scope` with a stable id
  (peer index, slice number). Seeds are then reproducible under any
  scheduling; wire-byte identity additionally requires one writer
  lane per channel, since concurrent writers to a single channel
  interleave nondeterministically even with deterministic seeds.
- **`PRG(const void*, int)` is unchanged in test mode.** Callers
  with their own explicit seed sources already control determinism;
  the test-mode hook only affects the OS-random default path.
- **`ECGroup::rand_scalar` uses a `thread_local PRG`.** Determinism
  per thread depends on the order of `rand_scalar` calls, which
  is determined by the protocol's call graph.
- **External inputs are not deterministic.** Test inputs (choice
  bits, message buffers) come from the caller — the test harness
  must seed them deterministically too.

## Recording a trace

`TraceIO` wraps any `IOChannel*` and writes a copy of every wire
byte to two files: `<prefix>.send` and `<prefix>.recv`. Bytes are
copied before outbound delivery and after inbound delivery. Trace
files are created with mode `0600`; `TraceIO::flush()` flushes both
files before flushing the wrapped channel.

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
# Record the "before" trace (both Alice and Bob).
EMP_TEST_MODE=1 ./run ./build/your_protocol_test before
# … apply your refactor / optimization, rebuild …
EMP_TEST_MODE=1 ./run ./build/your_protocol_test after

# Each direction of the wire, checked at its sender; both must be empty.
diff before.alice.send after.alice.send
diff before.bob.send   after.bob.send
```

A direction is diffed at its *sender*: the receiver's `.recv` file only
contains bytes the application consumed, so it can miss trailing bytes a
refactor sends that the peer never reads. The two `.send` files together
cover the full wire.

A reference harness lives in emp-ot at
[`test/trace_equiv.cpp`](https://github.com/emp-toolkit/emp-ot/blob/main/test/trace_equiv.cpp)
and may be a useful template for new wire-equivalence tests.

## What test mode does NOT cover

- **Transcript interleaving across lanes.** Lanes fix the seeds;
  they cannot fix the order in which concurrent lanes' bytes land on
  a *shared* channel. Keep one writer lane per traced channel (the
  per-thread-NetIO pattern).
- **Caller-provided randomness.** A `PRG` constructed with an
  explicit seed bypasses the test-mode hook entirely.
- **Other OpenSSL randomness.** Only `BN_rand_range` (via
  `ECGroup::rand_scalar`) is intercepted. Direct `RAND_bytes` calls,
  if any code path adds them, would still pull from OpenSSL's
  CSPRNG. As of the framework's introduction, no such call exists
  in emp-tool or emp-ot; `grep -rn "RAND_bytes\|BN_rand"` over
  both repos finds the single intercepted site.
- **Non-determinism from outside the toolkit.** Wall-clock time,
  PIDs, system-state-dependent paths — none of these affect wire
  bytes, but are flagged for completeness.
