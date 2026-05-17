# Test mode — deterministic randomness for wire-byte equivalence

emp-tool exposes a "test mode" flag that swaps every randomness
source in the toolkit for a deterministic, counter-derived stream.
With test mode on, the wire bytes a protocol produces are byte-
identical across runs of the same code. Two complementary headers
build the framework:

| Header | Role |
|---|---|
| [`emp-tool/core/test_mode.h`](../emp-tool/core/test_mode.h) | The toggle. `is_test_mode()`, `set_test_mode(bool)`, `next_test_seed()`, `reset_test_seed_counter()`. |
| [`emp-tool/io/trace_io.h`](../emp-tool/io/trace_io.h) | `TraceIO` IOChannel adapter that tees wire bytes to a pair of files alongside delivering them. |

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
   etc. In test mode, `PRG()` pulls a deterministic counter-derived
   seed from `next_test_seed()` instead of OS entropy
   (`/dev/urandom` or `RDSEED`).
2. **`ECGroup::get_rand_bn`** — the only call to OpenSSL's
   `BN_rand_range` in the toolkit. Used by P-256 base OTs (PVW,
   CSW, NP, CO). In test mode, it samples uniform in `[0, order)`
   via emp::PRG-driven rejection instead of OpenSSL's internal
   CSPRNG.

`PRG(const block*, int)` (explicit seed) is unaffected — callers
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

`reset_test_seed_counter()` rewinds the counter to its initial
value — call it between independent test iterations to get
reproducible PRG sequences within one process.

### Cost in production

One cached `std::atomic<bool>` load per `PRG()` default-
construction. Branch predicts perfectly; no measurable overhead
in production paths.

## Determinism contract

- **Single-threaded execution required.** Multiple threads
  consuming the global seed counter produce non-reproducible
  orderings. Protocols with internal threading need their own
  determinism story (typically: per-thread seed = `base ^ tid`,
  plus a deterministic merge order on the wire).
- **`PRG(const block*, int)` is unchanged in test mode.** Callers
  with their own explicit seed sources already control determinism;
  the test-mode hook only affects the OS-random default path.
- **`ECGroup::get_rand_bn` uses a `thread_local PRG`.** Determinism
  per thread depends on the order of `get_rand_bn` calls, which
  is determined by the protocol's call graph.
- **External inputs are not deterministic.** Test inputs (choice
  bits, message buffers) come from the caller — the test harness
  must seed them deterministically too.

## Recording a trace

`TraceIO` wraps any `IOChannel*` and writes a copy of every wire
byte to two files: `<prefix>.send` and `<prefix>.recv`. Bytes are
delivered to the underlying channel either before (recv) or
synchronously (send) with the file write, so a crash mid-write
leaves a trace prefix that still matches what the peer didn't yet
see.

```cpp
NetIO* under = new NetIO(...);
TraceIO* io  = new TraceIO(under, "before.alice");
// ... use `io` exactly like a NetIO; protocol writes get teed to
// before.alice.send / before.alice.recv ...
delete io;
delete under;
```

Counters: `TraceIO` inherits the `IOChannel::counter` book-keeping
from the base. The wrapped underlying channel's `counter` does NOT
increment for traced bytes — protocol code that observes
`io->counter` reads the wrapping `TraceIO`'s count, which is
correct.

## Verification workflow

For a refactor / optimization where wire-equivalence is required:

```bash
# Record the "before" trace (both Alice and Bob).
EMP_TEST_MODE=1 ./run ./build/your_protocol_test before
# … apply your refactor / optimization, rebuild …
EMP_TEST_MODE=1 ./run ./build/your_protocol_test after

# All four diffs must be empty.
diff before.alice.send after.alice.send
diff before.alice.recv after.alice.recv
diff before.bob.send   after.bob.send
diff before.bob.recv   after.bob.recv
```

A reference harness lives in emp-ot at
[`test/trace_equiv.cpp`](https://github.com/emp-toolkit/emp-ot/blob/master/test/trace_equiv.cpp)
and may be a useful template for new wire-equivalence tests.

## What test mode does NOT cover

- **Threading orderings.** See contract above.
- **Caller-provided randomness.** A `PRG` constructed with an
  explicit seed bypasses the test-mode hook entirely.
- **Other OpenSSL randomness.** Only `BN_rand_range` (via
  `ECGroup::get_rand_bn`) is intercepted. Direct `RAND_bytes` calls,
  if any code path adds them, would still pull from OpenSSL's
  CSPRNG. As of the framework's introduction, no such call exists
  in emp-tool or emp-ot; `grep -rn "RAND_bytes\|BN_rand"` over
  both repos finds the single intercepted site.
- **Non-determinism from outside the toolkit.** Wall-clock time,
  PIDs, system-state-dependent paths — none of these affect wire
  bytes, but are flagged for completeness.
