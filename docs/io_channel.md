# IO channel layer (`emp-tool/runtime/io/`)

Conventions for `NetIO` and any code that uses it. Read this when
writing protocol code that does sends / recvs / flushes across multiple
threads, or when investigating a NetIO deadlock.

## Flush contract

Callers MUST call `flush()` at the end of any protocol step that ends
in sends, before returning to the caller or blocking on anything other
than a recv on the same NetIO. The auto-flush only fires when the same
NetIO does a recv (recv_data_internal drains pending sends via
`flush_unlocked()` before blocking on the read); a
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

`test/runtime/test_netio.cpp` runs the correctness + send-only-regression coverage.
Throughput lives separately in `bench/bench_netio.cpp`.

## Thread-safety

NetIO is NOT thread-safe. The user-space `send_buf` coalescing has no
per-call lock; concurrent `send_data` / `recv_data` / `flush` from two
threads on the same instance corrupts the buffer. Each instance must be
owned by one thread at a time — `flush()` counts as a "touch" and is
unsafe to call from a thread other than the one currently sending.

## Thread-safety contract

A channel is not thread-safe: send-buffer coalescing is unlocked, and
TLSIO's `SSL*` mutates internal state on every read and write. Only one
thread may touch a given channel at a time; sequential hand-off between
threads is fine. Threaded consumers take a channel per thread —
`make_sibling()` on NetIO, or a per-lane channel at the protocol layer.
Races on a shared channel are not detected at runtime; when debugging a
threaded consumer, build with `-fsanitize=thread`.

## Fiat–Shamir transcript

The `IOChannel` base can keep two running hash transcripts — one over
every byte sent, one over every byte received — for deriving
Fiat–Shamir challenges from the wire. Off by default;
`enable_fs<opt>(send_first)` turns it on.

Cross-party contract: both parties MUST select the same backend `opt`
(default `EMP_FS_HASH_DEFAULT`, set stack-wide via CMake `EMP_FS_HASH`)
or every derived challenge diverges. Exactly one party passes
`send_first=true` — it fixes the concatenation order so both ends
compute the same digest. `enable_fs` is single-shot: calling it twice
asserts.

`get_digest()` returns the first 16 B of `H(d_AB ‖ d_BA)` over both
directions and is non-destructive (call it repeatedly to derive
sub-challenges across stages). `get_send_digest()` / `get_recv_digest()`
are per-direction snapshots for diagnostics. All three assert that
`enable_fs` ran first.

## Other base surface

- **`sync()`**: optional 1-byte ping/pong handshake to confirm both
  directions are alive. NetIO implements it; the base default is a
  no-op.
- **Telemetry**: the base tracks `send_counter` / `recv_counter` /
  `rounds` / `flushes_count`; `get_statistics_string()` renders them
  for logging (`~NetIO` prints it unless constructed `quiet`).
- **NetIO factories**: `NetIO::listen(port)` / `NetIO::connect(addr,
  port)` are named, ownership-returning replacements for the
  nullptr-means-server constructor; repeated `make_sibling()` calls on the
  primary or any related sibling open additional duplex channels to the same
  peer on the same port. Related server channels share listener ownership; the
  listener closes with the last one. This avoids closing, rebinding, or sleeping
  between accepts.
- **Multiple channels — recommended pattern**: to run several channels to
  the same peer, keep one anchor NetIO alive and take the others via
  `make_sibling()`, calling it **serially and in the same order on both
  parties** (its accept/connect pairing is FIFO on the shared listener —
  concurrent `make_sibling()` from multiple threads is not deterministic).
  Do *not* rely on closing every channel and reopening a new one on the
  same port as the coordination mechanism. That reopen path is supported
  and race-free — each connection is only considered established once the
  peer has actually `accept()`ed it (a one-byte accept acknowledgement in
  `tcp_socket.h` protects against a `connect()` landing on the previous,
  now-stale listener) — but the anchor + `make_sibling` pattern is simpler
  and avoids the reconnect entirely.
- **`TraceIO`** (`trace_io.h`): an `IOChannel` that tees every wire
  byte to `<prefix>.send` / `<prefix>.recv` files for diff-based
  wire-equivalence checks; see `test_mode.md`.

## TLS variant

`TLSIO` (in `emp-tool/runtime/io/tls_io_channel.h`) is another `IOChannel`
implementation for deployments where the wire crosses an untrusted network. Same flush
contract, same single-thread-owned discipline, same telemetry counters
as NetIO; the only difference is the wire — TLS 1.3 over OpenSSL
instead of raw TCP. Pin both ends of the protocol-version range to
TLS 1.3 (no negotiation surface), use the default socket BIO (we
already coalesce above SSL_write so `BIO_f_buffer` would just double-
buffer), and do a two-phase `SSL_shutdown` from the destructor so
buffered records actually leave the box before FIN.

Cert / key / CA material is caller-supplied via `TLSConfig` (PEM file
paths). mTLS is on by default — each side verifies the peer's
certificate chain against the configured CA; clear `require_peer_cert`
on the server to make client-cert presentation optional, or set
`insecure_skip_verify` in dev only. Verification is chain-only: no
expected-hostname / SAN / fingerprint binding, so any certificate
issued by the configured CA authenticates — the CA must be
deployment-private (see the README's Security section). One `SSL_CTX`
per channel; if a profile shows the per-channel cert-parse cost
mattering, share a CTX via a thin factory.
