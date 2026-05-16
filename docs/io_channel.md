# IO channel layer (`emp-tool/io/`)

Conventions for `NetIO` and any code that uses it. Read this when
writing protocol code that does sends / recvs / flushes across multiple
threads, or when investigating a NetIO deadlock.

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

`test/test_netio.cpp` runs the full correctness + send-only-regression
+ bench suite.

## Thread-safety

NetIO is NOT thread-safe. The user-space `send_buf` coalescing has no
per-call lock; concurrent `send_data` / `recv_data` / `flush` from two
threads on the same instance corrupts the buffer. Each instance must be
owned by one thread at a time — `flush()` counts as a "touch" and is
unsafe to call from a thread other than the one currently sending.

## Debug build assertion

Under `!NDEBUG`, NetIO carries an `_in_use` atomic
counter and `touch_guard` that wraps `send_data_internal` /
`recv_data_internal` / `flush`. If two threads enter any of those on
the same instance simultaneously, the build aborts with `NetIO race:
concurrent <op> on the same NetIO`. Zero cost under `-DNDEBUG`.

Use this when a multi-party protocol behaves flakily — race or no
race, the answer falls out of a Debug build.

## TLS variant

`TLSIO` (in `emp-tool/io/tls_io_channel.h`) is a third `IOChannel` for
deployments where the wire crosses an untrusted network. Same flush
contract, same single-thread-owned discipline, same telemetry counters
as NetIO; the only difference is the wire — TLS 1.3 over OpenSSL
instead of raw TCP. Pin both ends of the protocol-version range to
TLS 1.3 (no negotiation surface), use the default socket BIO (we
already coalesce above SSL_write so `BIO_f_buffer` would just double-
buffer), and do a two-phase `SSL_shutdown` from the destructor so
buffered records actually leave the box before FIN.

Cert / key / CA material is caller-supplied via `TLSConfig` (PEM file
paths). mTLS is on by default — both sides verify; clear
`require_peer_cert` on the server to make client-cert presentation
optional, or set `insecure_skip_verify` in dev only. One `SSL_CTX` per
channel; if a profile shows the per-channel cert-parse cost mattering,
share a CTX via a thin factory.
