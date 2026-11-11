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
NetIO whose step is purely sends — e.g. a receiver that ends a setup
step with a few KiB of sends and then waits on another channel —
strands its tail bytes in the user-space `send_buf` until something
else moves them.

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

## Session ids

A channel may hold a session root that both ends agree on when the connection
is set up. `next_session_id()` returns a fresh `SessionID` on every call, a
child (`SessionID::derive`) of that root. It is local: the two ends get the
same sequence by calling it in the same order, and another connection gets a
different one. The root itself is never returned. `has_session_root()` reports
whether the channel has one; asking a channel without a root for an id is an
error.

- **NetIO** agrees on its root with a commit-then-reveal coin flip in its
  connecting constructors (the address constructor, `listen` and `connect`).
  Right after its accept acknowledgement, the listener sends `c = RO(s ‖ r)`
  (a 32-byte digest); the connector replies with `r'`; the listener opens
  `s ‖ r`, and the connector checks the opening against `c`. The root is
  `RO(r ‖ r')`. The flip adds one round trip to setup on both ends. Like
  `sync()`, it runs on the transport hooks, so its bytes are not counted and
  not in the Fiat–Shamir transcript. The root is uniform when at least one
  party follows the protocol; the listener learns it first and can abort
  instead of opening, the bias any two-party coin flip allows. The flip does
  not authenticate the peer: on an unauthenticated network, a man in the
  middle runs one flip with each party. Pass `NetIO::SessionRoot::none` to
  skip the flip. Both ends must choose the same: a mismatch is not detected,
  so the setup hangs or the flip's bytes are read as protocol data.
- **Siblings**: `make_sibling()`, on NetIO and on TLSIO, gives the sibling
  its parent's next session id as its root, so NetIO siblings run no flip,
  and a sibling of a channel without a root has none. A NetIO adopted from a
  socket does not know which end of the connection it is, so it has no
  root.
- **TLSIO** takes its root from the TLS exporter (RFC 8446, section 7.5),
  under the private-use label `EXPERIMENTAL-emp-tool-session-id`. Both ends
  derive it from the handshake secret with no message, so the root is bound
  to the handshake with the peer that holds the pinned key.
- **TraceIO** forwards both calls to the channel it wraps, so the two share
  one sequence.

## Other base surface

- **`sync()`**: a barrier for tests, benchmarks and setup, not protocol logic.
  Each side sends one zero byte, flushes, and waits for the peer's, so it returns
  once both have entered it; neither side has to go first, and it works the same
  on every `IOChannel` (one implementation in the base). The bytes go through the
  transport hooks, so they are not counted and not in the Fiat-Shamir transcript;
  `TraceIO` records them. A nonzero byte means the two sides are out of step, and
  `sync()` reports it.
- **Telemetry**: the base tracks `send_counter` / `recv_counter` /
  `rounds` / `flushes_count`; `get_statistics_string()` renders them
  for logging (`~NetIO` prints it unless constructed `quiet`).
- **NetIO factories**: `NetIO::listen(port)` / `NetIO::connect(addr,
  port)` are named, ownership-returning alternatives to the
  nullptr-means-server constructor; repeated `make_sibling()` calls on the
  primary or any related sibling open additional duplex channels to the same
  peer on the same port. Related server channels share listener ownership; the
  listener closes with the last one. This avoids closing, rebinding, or sleeping
  between accepts.
- **Multiple channels — recommended pattern**: to run several channels to
  the same peer, keep one anchor channel (NetIO or TLSIO) alive and take the
  others via `make_sibling()`, calling it **serially and in the same order on
  both parties** (its accept/connect pairing is FIFO on the shared listener —
  concurrent `make_sibling()` from multiple threads is not deterministic).
  Closing all channels and reopening the same port is supported, but sibling
  channels avoid reconnecting.
- **`TraceIO`** (`trace_io.h`): an `IOChannel` that tees every wire
  byte to `<prefix>.send` / `<prefix>.recv` files for diff-based
  wire-equivalence checks; see `test_mode.md`.

## TCP socket buffers

By default emp-tool leaves socket buffers to the operating system, which grows
them with each connection: on Linux up to `net.ipv4.tcp_wmem` / `tcp_rmem`
(4 MiB send, 6 MiB receive by default), on macOS up to
`net.inet.tcp.autosndbufmax` / `autorcvbufmax` (4 MiB). That covers a path whose
bandwidth-delay product is up to about 4 MiB, such as 1 Gbit/s at 30 ms.

For a longer or faster path, `tcp::SocketOptions` sets the buffer sizes. They are
applied before `listen()` or `connect()`, so TCP window scaling is negotiated for
them. When the path's bandwidth and round-trip time are known, the helper picks the
sizes:

```cpp
using namespace std::chrono_literals;

auto options = tcp::SocketOptions::for_bandwidth_and_rtt(
    400'000'000, 100ms);  // 5 MB bandwidth-delay product -> 8 MiB buffers
auto io = NetIO::connect(peer, port, options);
```

It leaves both sizes at zero (the operating system's choice) when the
bandwidth-delay product is at most 4 MiB, and otherwise picks the first power of
two at or above it. Set the byte fields directly when the two directions need
different sizes. Pass the same options to `NetIO::listen`; accepted sockets inherit
them from the listener, and `make_sibling()` passes them to each new connection.
`TLSIO` takes them through `TLSConfig::socket_options` and applies them before the
TLS handshake; the adopted-socket TLS constructor requires default options.

An explicit size fixes the buffer, and on Linux it also stops the operating system
from growing it. The system limits explicit sizes: on Linux to
`net.core.wmem_max` / `net.core.rmem_max`, 212,992 bytes unless raised, and on macOS
to `kern.ipc.maxsockbuf`, 8 MiB unless raised; both cap a larger request without
an error. emp-tool checks the limit first. A request above it is lowered to the
limit when that is still more than the 4 MiB the operating system reaches on its own
(macOS by default), and otherwise not made, so the socket keeps the operating
system's defaults (Linux by default). Either way emp-tool prints one warning per
process with the command that raises the limit, for example

```
emp-tool: warning: for the 400 Mbit/s bandwidth and 100 ms round-trip time you gave,
8 MiB socket buffers would help throughput, but this system allows 208 KiB, so the
operating system's defaults are kept. To allow 8 MiB buffers, run:
  sudo sysctl -w net.core.rmem_max=8388608 net.core.wmem_max=8388608
```

On Linux, put the same settings in a file under `/etc/sysctl.d/` to keep them across
reboots.

## TLS variant

`TLSIO` (in `emp-tool/runtime/io/tls_io_channel.h`) is another `IOChannel`
implementation for deployments where the wire crosses an untrusted network. Same flush
contract, same single-thread-owned discipline, same telemetry counters
as NetIO; the only difference is the wire — TLS 1.3 over OpenSSL
instead of raw TCP. Pin both ends of the protocol-version range to
TLS 1.3 (no negotiation surface), use the default socket BIO (we
already coalesce above SSL_write so `BIO_f_buffer` would just double-
buffer). The destructor flushes, sends close_notify and closes without
waiting for the peer's close_notify, so channels can be destroyed in any
order, as with NetIO. A TLSIO server sends no session tickets (TLSIO
never resumes a session), so a send-only peer has no unread bytes that
would turn its close into a connection reset.

Authentication is by pinned public keys, with no CA. `TLSConfig` names
this party's private key (`key_pem_path`) and the peer's public key
(`peer_public_key_pem_path`, a PEM `BEGIN PUBLIC KEY` block), both
required. TLS 1.3 carries a key in a certificate, so each end presents a
throwaway self-signed certificate for its key and accepts the peer's only
if it carries the pinned key; the handshake signature proves that the peer
holds the matching private key. Any key type OpenSSL signs TLS 1.3
handshakes with works (Ed25519, ECDSA, RSA), and the two parties' types may
differ. A TLS 1.3 client finishes its handshake before the server checks
the client's key, so a client the server rejects fails at its next read
rather than in its constructor.

`TLSIO::make_sibling()` opens another TLS channel to the same peer and port,
as NetIO's does: a listening TLSIO keeps its listener open while it or a
sibling is alive, and both ends call it serially and in the same order. A
sibling shares its parent's `SSL_CTX` (the keys, read and signed into a
certificate once) but runs its own handshake, so its traffic keys are its
own; TLSIO never resumes a session, so nothing else carries over. A TLSIO
adopted from a socket cannot open siblings.
