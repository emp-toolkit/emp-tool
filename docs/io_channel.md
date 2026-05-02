# IO channel layer (`emp-tool/io/`)

Conventions for `NetIO`, `NetIOBuffered`, and any code that uses them.
Read this when writing protocol code that does sends / recvs / flushes
across multiple threads, or when investigating a NetIO deadlock.

## Two implementations, one contract

- Two NetIO implementations, same `IOChannel` contract: `NetIO` (default,
  `"wb"` stdio + raw `::read` on recv — faster on small-message protocol
  workloads) and `NetIOBuffered` (`"wb+"` stdio + `fread` on both paths
  — faster on multi-MiB bulk transfers). Pick `NetIO` unless you're
  specifically pushing bulk data; the rest of this section applies to
  both.

## Flush contract

Callers MUST call `flush()` at the end of any protocol step that ends
in sends, before returning to the caller or blocking on anything other
than a recv on the same NetIO. The auto-flush only fires when the same
NetIO does a recv (`flush()` at the top of `recv_data_internal`); a
NetIO whose step is purely sends — e.g. the IKNP receiver-role channel
during `setup_recv` — strands its tail bytes in the user-space
`send_buf` until something else moves them.

`~NetIO` flushes, so pure send-then-destruct works. Long-lived NetIOs
with a mid-life send-only step do not: the destructor only runs at
end-of-life, which doesn't happen until the protocol completes, which
can't complete because the peer is blocked on bytes stuck in send_buf.
This is a circular-wait deadlock, not "data eventually arrives slowly".

**Rule of thumb**: if the next thing you do on this NetIO isn't a recv,
flush first. Applies to function boundaries, phase boundaries within
a function, and any blocking wait (thread join, barrier, recv on a
*different* NetIO).

`test/netio.cpp` is templated on the IO type and runs the full
correctness + send-only-regression + bench suite against both classes
back-to-back.

## Thread-safety

NetIO and NetIOBuffered are NOT thread-safe. The user-space `send_buf`
coalescing has no per-call lock; concurrent `send_data` / `recv_data`
/ `flush` from two threads on the same instance corrupts the buffer.
Each instance must be owned by one thread at a time — `flush()` counts
as a "touch" and is unsafe to call from a thread other than the one
currently sending.

(The pre-rewrite NetIO didn't have this hazard because every send went
through `fwrite`, which serializes via stdio's `flockfile`. The
user-space buffer is faster but loses that implicit serialization.)

## Debug build assertion

Under `!NDEBUG`, NetIO and NetIOBuffered carry an `_in_use` atomic
counter and `touch_guard` that wraps `send_data_internal` /
`recv_data_internal` / `flush`. If two threads enter any of those on
the same instance simultaneously, the build aborts with `NetIO race:
concurrent <op> on the same NetIO`. Zero cost under `-DNDEBUG`.

Use this when a multi-party protocol behaves flakily — race or no
race, the answer falls out of a Debug build.
