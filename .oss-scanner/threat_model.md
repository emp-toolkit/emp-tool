# emp-tool threat model

## What emp-tool is

emp-tool is the base layer of a toolkit for secure multi-party computation
(MPC). It is a C++20 library in three layers:

- `emp-tool/runtime/`: `block`, crypto primitives (AES, PRG, hash, random
  oracle, CCRH/MITCCRH, GF(2^128), P-256 via OpenSSL, Rijndael-256), the IO
  channels (`NetIO`, `TLSIO`) and the per-gate garbling functions (half-gates,
  privacy-free).
- `emp-tool/ir/`: the `BooleanProgram` circuit IR, its `.empbc` file format,
  validation, transforms, and the session contracts.
- `emp-tool/circuits/`: circuit value types (`Bit_T`, `UInt_T`, `Int_T`,
  `Float_T`, ...) and in-circuit AES/SHA-256/Keccak.

OT and MPC protocols are built on top of emp-tool in separate libraries.
emp-tool supplies their primitives, and those primitives must stay safe when
the other parties are malicious, whatever protocol runs above them.

## Trust model and where untrusted input enters

emp-tool follows the usual MPC trust model.

- **Trusted:**
  - The local party's own code, compiled correctly from this source.
  - Everything the local party controls: its build configuration, its
    environment variables (`EMP_CIRCUIT_DIR`, `EMP_PORT`, `EMP_PEER_IP`),
    the files on its disk (circuit assets under `emp-tool/ir/files/`, PEM key
    files), and the calling program, including any `BooleanProgram` it builds
    in memory.
  - Whether the source itself can be trusted is a separate question, out of
    scope here.
- **Untrusted:** every other party the local party communicates with. Each
  one controls every byte it sends, may deviate from the protocol
  arbitrarily, may abort at any time, and may collude with the other
  parties.

So untrusted input enters only from other parties:

1. **Bytes from another party over an IO channel.** These are the most important
   inputs. All receives go through `IOChannel` (`runtime/io/io_channel.h`)
   over `NetIO` (`net_io_channel.h`, `tcp_socket.h`) or `TLSIO`
   (`tls_io_channel.h`).
   - Receives are exact-length and sized by the caller: `recv_data`,
     `recv_block`, `recv_bool`.
   - `recv_pt` is the one length-prefixed message. It decodes a P-256 point
     through `Point::from_bin` (`runtime/crypto/ec.cpp`), which must reject
     off-curve points, the point at infinity and oversized encodings
     (`MAX_POINT_BYTES`).
   - `recv_bool` must reject nonzero padding bits, so that each message has
     exactly one encoding in the Fiat-Shamir transcript.
   - The connection handshake has an accept byte and, on `NetIO`, a
     session-root coin flip (commit-then-reveal).
   - `TLSIO` runs a TLS 1.3 handshake with a pinned peer key and derives the
     session root from the TLS exporter.
2. **Garbled tables.** A protocol library receives them with `recv_block` and
   passes them to `halfgates_eval` (`runtime/execution/half_gate.h`) or
   `privacy_free_eval` (`privacy_free.h`). The garbler must not learn the
   evaluator's inputs. The evaluator must not learn the global offset Δ or
   any label it was not given. The per-gate tweak in `mitccrh.h` is
   security-critical, because a tweak collision between gates leaks Δ.
3. **Network attacker.**
   - On `TLSIO`, an attacker without the pinned private key must not be able
     to impersonate a party or read or alter traffic.
   - `NetIO` is plaintext and unauthenticated by design. It assumes a trusted
     network.
4. **`.empbc` circuit files from another party** (`ir/empbc.h`).
   - Circuit files that ship with the source, or that the local party
     produced, are trusted.
   - A file obtained from another party is not. The loader is built for that
     case and is documented as hostile-input safe. `load_empbc` and
     `load_empbc_file` must reject malformed input before allocating or
     executing anything: bounds-checked reads, a cap on the wire count
     (`max_wires`), an exact size check, then `validate_program`
     (`ir/validate.h`).

## Security properties emp-tool must keep against other parties

- No memory-safety bug or undefined behaviour reachable from bytes another
  party sends, or from a `.empbc` file another party supplies.
- No leak of local secrets: PRG seeds and outputs, Δ, wire labels, private
  keys, TLS traffic secrets.
- No acceptance of malformed or non-canonical values that a protocol relies
  on being rejected.
- No resource use beyond what the caller asked to receive.
- Every check on data from another party stays active in Release builds. Checks use `expecting(...)` / `error()` (`runtime/core/error.h`).
  A guard that exists only as `assert()` is a bug.

## Components, most important first

1. `runtime/io/`: the receive paths, the handshakes, TLS pinned-key
   authentication, and the Fiat-Shamir transcript (`enable_fs`).
2. `runtime/crypto/`:
   - `ec.cpp` / `ec.h` (point decoding)
   - `mitccrh.h`, `ccrh.h` and `runtime/execution/` (garbling)
   - `prg.h` (seeding via `getentropy`)
   - `hash.h`, `ro.h`, `session_id.h`, `aes.h`, `f2k.h`, `rijndael256.h`
3. `ir/empbc.h`, `ir/validate.h`, `ir/assets.cpp`, `ir/builtins.cpp`.
4. `circuits/`: no external input surface. Wrong results in arithmetic,
   floating point or in-circuit crypto still matter, because protocols run
   these circuits on secret data.
5. `tools/empbc_tool.cpp`: a local command-line tool (`inspect`, `compare`,
   `manifest`, `check-manifest`).

## How to exercise it

The image has two builds of `/src`:

- `/src/build`: GCC, RelWithDebInfo, default configuration.
- `/src/build-san`: Clang, ASan + UBSan.
  - Run its binaries with
    `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`,
    as CI does.

Ways to run things:

- `ctest --test-dir /src/build-san -j"$(nproc)"` runs everything.
  Test binaries sit directly in the build directory.
- Two-party tests run both parties locally through the `run` script:
  `./run /src/build-san/test_tlsio`, from `/src`.
  - The port comes from `EMP_PORT` (default 12345). Party 1 listens and
    party 2 connects to `EMP_PEER_IP` (default 127.0.0.1).
  - A two-party reproducer is easiest as one binary that takes the party id
    as `argv[1]`, with one party sending crafted bytes through `send_data`.
    `test/runtime/test_netio.cpp` and `test/runtime/test_tlsio.cpp` are
    templates.
- `.empbc` parsing: `test/ir/test_boolean_program.cpp` has malformed-file
  cases. To parse a file directly, run
  `/src/build-san/empbc-tool inspect <file>`.
- Point decoding: `test/runtime/test_ecc.cpp`.
- Padding and transcript checks: `test/runtime/test_io_channel.cpp`.
- Garbling: `test/runtime/test_halfgate.cpp`.
- Other configurations can be built offline from `/src`, because the
  BLAKE3 backend is vendored:
  - `-DEMP_HASH_BACKEND=blake3`
  - `-DEMP_FS_HASH=blake3`
  - `-DEMP_WITH_BLAKE3=ON`

## How we rate severity

- **Critical:** using only the messages they send, one or more other parties
  (or, on `TLSIO`, a network attacker) do one of these:
  - learns a local secret (Δ, PRG seed or output, a private key, TLS traffic
    secrets, the honest party's inputs);
  - gets code execution, or a controlled out-of-bounds write;
  - bypasses `TLSIO` pinned-key authentication.
- **High:**
  - Memory corruption (out-of-bounds read or write, use-after-free, integer
    overflow into a size) reachable from another party's bytes or from a
    `.empbc` file it supplies, without a demonstrated exploit.
  - A primitive that silently accepts a malformed value a protocol relies on
    rejecting: an invalid point, a non-canonical encoding, two messages with
    one transcript.
  - A check on another party's data that compiles out in Release.
  - A tweak or key-reuse flaw in garbling or the hash functions that weakens
    their stated security.
- **Medium:**
  - Another party making the process allocate or work far beyond what the
    caller asked to receive.
  - A hang another party can trigger. An abort is not a hang.
  - A wrong result from a `circuits/` function on some inputs.
  - Secret-dependent timing in code that claims stable timing
    (`f2k.h` `vector_inn_prdt_sum_red`).
- **Low:**
  - Issues that need the caller to break a documented precondition.
  - `empbc-tool` issues.
  - Secret-dependent timing in code that makes no constant-time claim
    (see the README's Security section), with no shown remote leak.
  - Memory leaks other parties cannot grow without bound.
  - Hardening suggestions.

## Not vulnerabilities

- **Fail-stop by design.** A failed check ends the process via `std::_Exit(1)`
  with no cleanup. Another party that triggers this abort, or that closes
  the connection, is expected. Any party can always abort a protocol.
- **Attacks that need control of the local party.** Anything that needs
  control of the local party's build, binary, toolchain, environment
  variables, files or calling code is out of scope. That includes circuit
  assets and any directory named by `EMP_CIRCUIT_DIR`. A compromised source
  tree is out of scope too.
- **Documented `NetIO` limits.**
  - `NetIO` has no authentication or encryption.
  - The session-root coin flip has a man in the middle and a listener that
    can abort after seeing the root, both documented in
    `net_io_channel.h`.
  - The `sync()` byte, the accept byte and the coin-flip messages sit outside
    the counters and the Fiat-Shamir transcript by design.
- **Trust in validated programs.** `execute_program` assumes a program that
  passed `validate_program`. Running a hand-built, unvalidated
  `BooleanProgram` is misuse.
- **Variable time on public inputs.** `hash_to_point` and
  `RO::squeeze_point` are documented as variable-time and take only public
  inputs.
- **Deliberate unaligned access.** Unaligned accesses through `memcpy` and
  `loadu` intrinsics in the SIMD and transpose code are deliberate. Alignment
  `assert`s on caller-owned buffers in `aes.h` are debug aids, not
  checks on another party's data.
- **Not thread-safe by contract.** Channels and `ECGroup` are documented as
  not thread-safe. Concurrent use is misuse.
- **Non-production code.**
  - `EMP_DETERMINISTIC_MODE` builds: deterministic randomness, insecure by design and
    never deployed.
  - `bench/`, `test/` (including fixed `/tmp` key paths in `test_tlsio`), the
    `run` script and `TraceIO`.
  - `ClearCtx` and `DigestCtx`. `DigestCtx` is documented as not a
    cryptographic hash.
- **Vendored code.** `third_party/` (BLAKE3), `emp-tool/third_party/sse2neon.h`
  and `emp-tool/third_party/ThreadPool.h` are out of scope, unless the bug is
  in emp-tool's changes to them or its use of them.
- **Protocol libraries.** The security of protocols built on emp-tool belongs
  to those libraries, unless the root cause is in an emp-tool primitive.
