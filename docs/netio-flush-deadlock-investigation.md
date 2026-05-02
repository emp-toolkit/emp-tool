# NetIO send-only-deadlock investigation (nP>=3 ref tests)

## The bug

At nP=3 on ref/emp-agmpc dev branch, the test suite mostly fails — tests
either timeout or fail with `OT Extension check failed`. At nP=2 most
pass. At baseline ref commit `39f9f74` (pre-refactor, when emp-agmpc
was self-contained with its own embedded `utils/`, `io/`, `ot/`),
11/11 pass at nP=3 in 5.4s. The regression was introduced by the
migration from emp-agmpc's embedded copies to upstream
`emp-tool` / `emp-ot`.

## What we ruled out

| Hypothesis | Verdict |
|---|---|
| Parallel base-OT setup race in auth_share_pool | Serializing didn't help (rerolled flakiness) |
| Parallel bulk rcot race in auth_share_pool | Serializing made it strictly worse (0/25 runs) |
| Misdirected `io->flush(peer)` calls in auth_share_pool lambdas | Cosmetic; not the cause |
| PRG output difference (`AES_set_encrypt_key` vs `AES_opt_key_schedule`) | They're aliases — identical output |
| Recent uncommitted emp-tool changes (`block.h` BlockVec, `net_io_channel.h` existing-fd ctor) | Additive, required for ref to build, not the bug |

## Root cause

The minimal 3-party reproducer (`ref/test/three_party_iknp.cpp`, just
IKNP setup + rcot, no auth_share_pool logic) **deterministically times
out at every emp-ot commit since `aa46057`** (the IKNP detempling
commit), but **passes at baseline 39f9f74**.

Mechanism:

1. NetIOMP creates **2 NetIOs per peer-pair** (`ios[peer]` and
   `ios2[peer]`), one for each direction.
2. auth_share_pool wires the IKNP-sender role (`abit1[peer]`) to one
   NetIO and the IKNP-receiver role (`abit2[peer]`) to the other.
3. The IKNP-receiver-role NetIO is **send-only across its entire
   lifetime** — `setup_recv` ends with unflushed `OTCO::send` writes,
   `rcot_recv` only sends, `rcot_recv_end` only sends. **No recv
   ever happens on it.**
4. Upstream NetIO has an auto-flush hook at the top of
   `recv_data_internal`, but it only flushes the *same* NetIO's send
   buffer. Since this NetIO never recvs, the auto-flush never fires.
   Pending sends stay stranded in user-space buffers.
5. The peer's IKNP-sender role (different NetIO at the peer) blocks
   reading those stranded bytes → deadlock.

**Why this didn't happen at baseline:** the embedded NetIO used
`fdopen(sock, "wb+")` (bidirectional stdio) with
`if (has_sent) fflush(stream)` at the start of every recv. C's stdio
rules force a flush on direction reversal anyway. So whenever the same
fd had recvs after sends, auto-flush was triggered. The new
write-only-stdio + raw-read design lost that guarantee.

## Why some tests fail with "OT Extension check failed" instead of timing out

In the minimal reproducer, no later protocol phase exists — pure
deterministic timeout. In the full ref suite, AuthSharePool's
`check2_*` and TriplePool's later phases do additional network I/O,
and *some* of those operations happen to be recvs on the
previously-stranded NetIOs. Those auto-flush old buffered bytes — but
at the wrong time relative to the Fiat-Shamir transcript / chi-fold
computation. The bytes arrive correctly but **at the wrong moment**,
scrambling the chi-fold ordering between sender and receiver. That
trips the malicious check rather than producing a clean deadlock.

## Fixes attempted

| Fix | Effect |
|---|---|
| `io->flush()` at end of `IKNP::setup_recv` | Setup phase now passes |
| `io->flush()` at end of `IKNP::rcot_recv_end` | rcot phase now passes; minimal reproducer 5/5 green |
| Combined | full ref suite at nP=3: 3/12 pass — improvement, but AuthSharePool/TriplePool's check2_* and other send-only sequences have the **same class of bug** at higher protocol layers |

## Options forward (independent)

| Plan | Scope | Pros | Cons |
|---|---|---|---|
| **A. Tactical patches** | Find every send-only sequence in upper layers (check2, EchoBC, compute, etc.), add `flush()` at end | Localized | Brittle; many sites; new code reintroduces the bug |
| **B. Bidirectional NetIO** | Revert NetIO to `fdopen("wb+")` + auto-flush on direction reversal (the baseline design) | Closes the entire bug class structurally; one-file change | Loses the "write-only stdio + raw read" perf benefit |
| **C. 1 NetIO per pair** | Collapse 2-NetIO pair design to 1 shared NetIO; both abit1 and abit2 use it | Both directions share a NetIO, recvs naturally auto-flush sends | Requires NetIO to be safe for concurrent send-from-thread-A + recv-from-thread-B |
| **D. Aggressive flush in NetIO** | Flush on send when accumulated bytes exceed N, or background flush thread | Incremental | Heuristic; can still miss small final sends |

Decision: pursuing **B**.

## Reproducer

`ref/test/three_party_iknp.cpp` — strips auth_share_pool down to bare
IKNP setup + bulk rcot, parallel across peers. Pass/fail at nP=3 is
the cleanest signal for whether NetIO's flush model is correct.
