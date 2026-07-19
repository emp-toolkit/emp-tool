# emp-tool

![build](https://github.com/emp-toolkit/emp-tool/workflows/build/badge.svg)
[![CodeQL](https://github.com/emp-toolkit/emp-tool/actions/workflows/codeql.yml/badge.svg)](https://github.com/emp-toolkit/emp-tool/actions/workflows/codeql.yml)

<img src="https://raw.githubusercontent.com/emp-toolkit/emp-readme/master/art/logo-full.jpg" width=300px/>

> **Which version do I want?**
>
> - **Existing projects pinned to a published release: stay on `0.3.0`** —
>   tag [`0.3.0`](https://github.com/emp-toolkit/emp-tool/releases/tag/0.3.0)
>   or branch [`v0.3.x`](https://github.com/emp-toolkit/emp-tool/tree/v0.3.x).
>   Bug fixes and security patches will be backported to `v0.3.x`.
> - **New projects: use `v1.0.0-alpha.1`** —
>   tag [`v1.0.0-alpha.1`](https://github.com/emp-toolkit/emp-tool/releases/tag/v1.0.0-alpha.1).
>   The API may still change from one alpha tag to the next and before the
>   final `1.0.0` release. CMake package metadata remains numeric `1.0.0`
>   because `project(VERSION)` cannot carry prerelease suffixes.

Foundational primitives for the emp-toolkit family: SIMD `block` types,
fast AES / PRG / PRP / hash / GF(2^128) kernels, OpenSSL-backed elliptic
curve ops, IO channels, and a boolean-circuit layer built around
context-bound typed values (`Bit_T<Ctx>` / `BitVec_T<Ctx,N>` /
`UInt_T<Ctx,N>` / `Int_T<Ctx,N>` / `Float_T<Ctx,W>`) with a compile-once /
run-on-any-context frontend. A `BooleanContext` is the execution target: plaintext evaluation
(`ClearCtx`), program recording (`RecordCtx`), and protocol contexts such as
emp-sh2pc's garbled `SH2PCCtx`.

## Requirements

- CMake ≥ 3.25
- A C++20 compiler (Clang ≥ 12, GCC ≥ 10, AppleClang 14+; CI exercises
  the current GCC / Clang / AppleClang releases — the stated minimums
  are not CI-tested)
- OpenSSL ≥ 3.0
- pthreads
- Linux or macOS on x86_64 with AES-NI + PCLMULQDQ + SSE4.2, or arm64
  with `armv8-a+crypto+crc`. Other platforms fail at CMake configuration time.

## Build and install

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build           # respects CMAKE_INSTALL_PREFIX
```

The default Release build uses `-O3 -funroll-loops` and tunes for the build
host's ISA (`-march=native` on x86_64 or `-mcpu=native` on arm64). This enables
the best available SIMD tiers, but the resulting binary may SIGILL on older
CPUs. Set `-DEMP_TOOL_NATIVE_ARCH=OFF` for the portable AES-NI + PCLMUL +
SSE4.2 x86_64 baseline or `armv8-a+crypto+crc` arm64 baseline. Default PRG
construction obtains its seed from `/dev/urandom` on every supported platform.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `EMP_TOOL_NATIVE_ARCH` | `ON` | Tune for the build host's ISA. Set `OFF` for a portable baseline binary. |
| `EMP_TOOL_BUILD_TESTS` | `ON` when top-level | Build the test suite under `test/`. |
| `EMP_TOOL_BUILD_BENCHMARKS` | `OFF` | Build throughput benchmarks under `bench/`; not registered with `ctest`. |
| `EMP_TOOL_BUILD_TOOLS` | `ON` when top-level | Build command-line tools such as `empbc-tool`. |
| `EMP_TOOL_INSTALL` | `ON` when top-level | Generate install + export rules. |
| `EMP_WITH_BLAKE3` | `OFF` | Compile the vendored BLAKE3 backend so `HashT<HashOption::blake3>` is usable while the default `Hash` stays `sha256`. Auto-`ON` when `EMP_HASH_BACKEND` or `EMP_FS_HASH` is `blake3`. |
| `EMP_HASH_BACKEND` | `sha256` | Default `emp::Hash` backend (`sha256` or `blake3`); changes every hash use, transcript-wide. Inherited by consumers. |
| `EMP_FS_HASH` | `default` | Fiat–Shamir transcript hash only (`default` = follow `EMP_HASH_BACKEND`, `sha256`, `blake3`); commitments/RO keep `emp::Hash`. Inherited by consumers; both parties must build with the same value. |

## Consuming from another CMake project

After `cmake --install build`:

```cmake
find_package(emp-tool CONFIG REQUIRED)
target_link_libraries(my-app PRIVATE emp-tool::emp-tool)
```

Without installing, the build tree exports its own targets file:

```cmake
find_package(emp-tool CONFIG REQUIRED PATHS /path/to/emp-tool/build)
target_link_libraries(my-app PRIVATE emp-tool::emp-tool)
```

Or as a subdirectory:

```cmake
add_subdirectory(third_party/emp-tool)
target_link_libraries(my-app PRIVATE emp-tool::emp-tool)
```

A single header pulls in the whole library (runtime + ir + circuits):

```cpp
#include <emp-tool/emp-tool.h>
using namespace emp;
```

Code that needs only one layer includes that layer's umbrella
(`emp-tool/runtime/runtime.h`, `emp-tool/ir/ir.h`, `emp-tool/circuits/circuits.h`).
The context-bound circuit values live in `emp-tool/circuits/typed.h` and the
frontend in `emp-tool/circuits/frontend/`; include those directly when you write
circuits (see "Circuit frontend" below).

A **session** owns the I/O boundary and protocol state; it exposes a **direct
context** via `ctx()` (`Session` / `DirectSession` in `ir/session/`). Circuit
values are context-bound (`UInt_T<ctx_t,N>` etc.), so a session names no value
family and adding a value family needs no session edit — `input`/`reveal` are
generic over any `WireValue`. `ClearSession` is the trivial plaintext session;
protocol libraries (emp-sh2pc, emp-ag2pc) provide their own over a garbled context.

## Layout

Three layers, depending **circuits → ir → runtime**. Each has an umbrella
(`runtime/runtime.h`, `ir/ir.h`, `circuits/circuits.h`); `emp-tool/emp-tool.h`
includes all three.

```
emp-tool/
├── runtime/      substrate
│   ├── core/        block, constants, utils, test_mode
│   ├── crypto/      PRG, PRP, AES, Hash, CCRH, MITCCRH, f2k, ec
│   ├── io/          IOChannel, NetIO, TLSIO, TraceIO
│   └── execution/   half-gate / privacy-free garbling leaf primitives
├── ir/           context-free Boolean IR + execution contracts
│   ├── program/validate/visit/passes/execute/schedule + .empbc assets/builtins/artifact
│   ├── wire_value.h   the generic WireValue concept
│   ├── context/       BooleanContext concept + Clear/Record/Count/Digest contexts
│   └── session/       Session / DirectSession / SessionIO contracts + ClearSession
├── circuits/     concrete value families + circuit libraries + frontend
│   ├── typed values (Bit_T/BitVec_T/UInt_T/Int_T/Float_T<Ctx>) + numeric kernels + sort
│   ├── crypto/        in-circuit AES-128 / SHA-256 / Keccak
│   └── frontend/      compile / run pure circuit functions on any context (emp::frontend)
└── third_party/  ThreadPool, sse2neon
```

The vendored BLAKE3 sources live at repo-root `third_party/blake3/` and are
compiled into `libemp-tool` when `EMP_WITH_BLAKE3` is enabled (directly, or
via a blake3 backend selection).

The canonical circuit value layer is the context-bound typed values in
`circuits/typed.h`: `Bit_T<Ctx>`, `UInt_T<Ctx,N>`, `Int_T<Ctx,N>`,
`Float_T<Ctx,W>`, and `BitVec_T<Ctx,N>`, each templated on a `BooleanContext`
`Ctx`. There is no global backend — every value carries its context and issues
value-return gates on it.

The numeric layer makes signedness explicit: `UInt_T<Ctx,N>` wraps mod 2^N
matching `uint{N}_t`, `Int_T<Ctx,N>` is two's-complement matching `int{N}_t` on
hardware (C signed-overflow UB is sidestepped — emp-tool wraps
deterministically). `Float_T<Ctx,W>` carries IEEE binary{16,32,64} values:
`+ − × ÷` / `min` / `max` are correctly rounded, `fma` is unfused (two
roundings), and `sqrt` / `recip` / `rsqrt` are approximate — see
[docs/floating_point_circuits.md](docs/floating_point_circuits.md). Comparisons
return `Bit_T<Ctx>`; the host clear types are `bool` / `uint64_t` / `int64_t` /
the host float.

## Usage

### PRG

```cpp
PRG prg;                                         // secure random seed
block rand_block[3];
int rand_int;

prg.random_block(rand_block, 3);                 // 3 × 128 random bits
prg.random_data_unaligned(&rand_int, 4);         // arbitrary-aligned dest

prg.reseed(&rand_block[1]);                      // reset seed + counter
```

`random_data` (16B-aligned) is the fast path; use `random_data_unaligned`
for any destination that isn't naturally 16-byte aligned (stack ints,
small structs, etc.) — the aligned variant asserts in debug.

### PRP / CCRH

`PRP` is the bare AES wrapper; the hash variants sit on top of it:

| Class | Models |
|---|---|
| `CCRH`    | circular correlation-robust hash |
| `MITCCRH` | multi-instance tweakable CCRH |

```cpp
block key;
PRG().random_block(&key, 1);

PRP prp(key);
block buf[64];
prp.permute_block(buf, 64);                      // in-place AES of 64 blocks

CCRH ccrh;
block out[8];
ccrh.H<8>(out, buf);                             // compile-time batch
block one = ccrh.H(buf[0]);                      // single-block form
```

CCRH has three call shapes: a scalar `H(block)` returning one block, a
templated batched `H<n>(out, in)` that the compiler unrolls (best for small
`n`), and a runtime `Hn(out, in, n)` for large batches. MITCCRH has a
different shape — see `crypto/mitccrh.h`. `CCRH` is the single
correlation-robust hash: its `sigma` preprocessing rules out a footgun class
of misuse where a plain CRH leaves `H(in)` and `H(in ⊕ Δ)` correlated.

### Hash

```cpp
Hash hash;
char data[1024];
char dig[Hash::DIGEST_SIZE];                     // 32 bytes

hash.put(data, sizeof(data));
hash.digest(dig);                                // resets after digesting
```

`Hash` is an alias for `HashT<>` — SHA-256 by default, the vendored BLAKE3
when built with `-DEMP_HASH_BACKEND=blake3`. `DIGEST_SIZE` is 32 bytes
either way.

### Random oracle

`RO` frames heterogeneous fields into one domain-separated transcript and
squeezes a digest. The domain string and session id are mandatory — sharing
them across protocols defeats the separation.

```cpp
block sid;
PRG().random_block(&sid, 1);

RO ro("my-protocol:v1", sid);                    // domain + session id
ro.absorb((uint64_t)7).absorb(sid);              // typed, length-framed fields
block h = ro.squeeze_block();                    // or squeeze_digest / squeeze_point
```

### GF(2^128) multiplication

`block` is a typedef for `__m128i`, so the f2k kernels accept it directly.

```cpp
block a, b, c;
PRG prg;
prg.random_block(&a, 1);
prg.random_block(&b, 1);
gfmul(a, b, &c);                                 // c = a · b in GF(2^128)
```

### Elliptic curves

`ECGroup` wraps an OpenSSL P-256 `EC_GROUP` + `BN_CTX`. P-256 is the only
supported curve; the legacy NID constructor parameter is retained for source
compatibility but rejects every value except `NID_X9_62_prime256v1`.
`Scalar` and `Point` are the corresponding handles.

```cpp
ECGroup G;                                       // P-256 by default
Scalar a = G.rand_scalar();                      // uniform in [0, order)
Point P = G.mul_gen(a);                          // P = a · G_generator

// Hash to curve, RFC 9380 §6 SSWU_RO_. Each protocol must pick its
// own domain-separation tag (DST); there's no default — sharing a
// DST across protocols defeats the point.
const char dst[] = "my-protocol:v1";
Point T = G.hash_to_point("my message", 10, dst, sizeof(dst) - 1);
```

### Network IO

```cpp
auto io = (party == ALICE) ? NetIO::listen(12345)
                           : NetIO::connect("127.0.0.1", 12345);
io->send_data(buf, n);                           // buffered
io->flush();                                     // drain outbound
io->recv_data(buf, n);                           // blocks until n bytes arrive
```

Every channel counts bytes, rounds, and flushes — `io->get_statistics_string()`
prints them. `TLSIO` (`runtime/io/tls_io_channel.h`) is a drop-in `IOChannel`
speaking TLS 1.3 under the same flush/threading contract, configured via
`TLSConfig`. See [docs/io_channel.md](docs/io_channel.md) for the channel
contract.

### Fiat–Shamir transcripts

Any `IOChannel` can hash everything it sends and receives:

```cpp
io->enable_fs(party == ALICE);                   // exactly one party passes true
// ... protocol traffic ...
block chal = io->get_digest();                   // both parties derive the same block
```

Both parties must select the same transcript hash (CMake `EMP_FS_HASH`).
`get_send_digest()` / `get_recv_digest()` snapshot one direction — a file-free
alternative to `TraceIO` for wire-regression checks.

### Plaintext circuit evaluation

`ClearCtx` is the plaintext `BooleanContext`: it evaluates typed values in
cleartext with no crypto, so a circuit's gate counts match what a protocol
context would run exactly. Build typed values over it and operate directly:

```cpp
#include <emp-tool/emp-tool.h>
using namespace emp;

ClearSession sess;                               // owns a ClearCtx + the I/O boundary
using Ctx = ClearSession::ctx_t;                 // the gate context values are built over
using S32 = Int_T<Ctx, 32>;

auto a = sess.input<S32>(ALICE, 7);              // feed inputs through the session
auto b = sess.input<S32>(BOB,   35);
auto c = a * b + S32::constant(sess.ctx(), 1);          // pure value-return gates; +1 is a public constant

std::cout << sess.reveal(c, PUBLIC).value() << "\n";  // reveal -> std::optional<clear_t>

// Wrap on overflow is well-defined and matches int32_t / uint32_t hardware:
using U32 = UInt_T<Ctx, 32>;
auto big = sess.input<U32>(ALICE, UINT32_MAX);
auto wrapped = big + U32::constant(sess.ctx(), 1u);   // == 0
```

`UInt_T` wraps mod 2^N, `Int_T` is two's-complement, `Float_T` carries IEEE
binary{16,32,64} values (per-operation semantics:
[docs/floating_point_circuits.md](docs/floating_point_circuits.md)), and
comparisons return `Bit_T<ClearCtx>`. The same typed
circuit code runs over any `BooleanContext` unchanged; only the session that
feeds inputs and reveals outputs differs — a protocol session over a garbled
context in place of `ClearSession`. Pure circuit bodies never do I/O. `reveal`
returns `std::optional<clear_t>` — the value on a party that learns it, `std::nullopt`
otherwise (a plaintext `ClearSession` always populates it).

### Circuit frontend: compile once, run on any context

Write a **pure circuit function** (inputs are arguments, the output is the return
value — no `input`/`reveal` inside) over the typed values
`Bit/BitVec/UInt/Int/Float<Ctx>`. Call it live, or **compile it once into a context-free `Circuit`** and `run` it on
any context — plaintext, garbled 2PC, ZK — with no global backend. I/O is the
context's job, around the circuit. Add `#include <emp-tool/circuits/frontend/circuit_fn.h>`.

```cpp
#include <emp-tool/circuits/frontend/circuit_fn.h>
#include <emp-tool/circuits/frontend/rec.h>
#include <emp-tool/ir/session/clear_session.h>
using namespace emp;
namespace cf = emp::frontend;

auto add  = [](auto a, auto b){ return a + b; };               // pure circuit (implicit ctx)
auto circ = cf::compile<rec::UInt<32>, rec::UInt<32>>(add);    // record ONCE -> Circuit

ClearSession sess;                                             // ... then run on any session's context
using Ctx = ClearSession::ctx_t;
auto x = sess.input<UInt_T<Ctx,32>>(ALICE, 7);
auto y = sess.input<UInt_T<Ctx,32>>(BOB,   5);
auto z = cf::run(sess.ctx(), circ, x, y);                      // replay -> UInt_T<Ctx,32> (== 12)
```

The same `circ` runs identically on the plaintext session, the garbled `SH2PCCtx`, and
future contexts — user circuits are as portable as the built-in `.empbc` files.
Arguments are named by the recording value types (`rec::UInt<32>`, `rec::Bit`,
`rec::Float<32>`, …); the compiled `Circuit` holds a validated `BooleanProgram` +
signature. Bodies are C++20: an implicit-context form (`[](auto a, auto b){…}`,
constants via `a.constant(v)`) and an explicit-context form (`[](auto& ctx, …){…}`,
required for nullary circuits). See [docs/frontend.md](docs/frontend.md).

### Native circuit files (`.empbc`)

Circuits load from the native binary `.empbc` format into one
`emp::circuit::BooleanProgram` (flat: inputs are wires `[0, num_inputs)`,
outputs are an explicit wire list) and replay through any `BooleanContext`. The
loader validates structure (bounds, single-definition, topological order) and
rejects malformed files. Assets resolve from `$EMP_CIRCUIT_DIR` (if set), then
the build tree, then the install tree — point `EMP_CIRCUIT_DIR` at assets
staged anywhere else. Floating-point `.empbc` assets ship in
`emp-tool/ir/files/`; see
[docs/floating_point_circuits.md](docs/floating_point_circuits.md) for the
asset format and regeneration notes. You can also `compile` your own pure
circuit function (above) or capture a recorded program and load it through this
API.

```cpp
#include <emp-tool/ir/context/context.h>   // execute_program
#include <emp-tool/ir/session/clear_session.h>
#include <emp-tool/ir/empbc.h>     // load_empbc_file
using namespace emp;
using namespace emp::circuit;

BooleanProgram program = load_empbc_file("my_circuit.empbc");

using Ctx = ClearSession::ctx_t;                         // any BooleanContext
Ctx ctx;
std::vector<Ctx::Wire> inputs(program.num_inputs);
// ... fill inputs (the leading wires) ...
std::vector<Ctx::Wire> out =
    execute_program(ctx, program,
                    std::span<const Ctx::Wire>(inputs.data(), inputs.size()));
```

`execute_program(ctx, program, inputs)` walks the gate list issuing the
context's value-return gate ops, so the same loaded program runs on the
plaintext context, the garbled `SH2PCCtx`, or any other context unchanged. A bulk/round-sensitive
context can consume the AND-depth schedule instead (`make_scheduled_plan` +
`scheduled_execute_program`).

### Circuit statistics and asset manifests

Top-level builds also produce `empbc-tool`. It uses the same hostile-input-safe
loader and IR passes as the library:

```bash
./build/empbc-tool inspect emp-tool/ir/files/aes128.empbc
./build/empbc-tool inspect --json emp-tool/ir/files/aes128.empbc
./build/empbc-tool compare old.empbc new.empbc
./build/empbc-tool manifest --output emp-tool/ir/files/manifests emp-tool/ir/files
./build/empbc-tool check-manifest emp-tool/ir/files/manifests emp-tool/ir/files
```

Reports include SHA-256, format/index width, stored and dense wire counts, gate
mix, AND depth and maximum level width, output-rooted reachability, dead gates,
peak live wires, maximum fanout, and stable program digests. The checked-in
`emp-tool/ir/files/manifests/` directory keeps one compact JSON baseline per
asset, making binary drift and major complexity changes visible in focused
code-review diffs. Set `EMP_TOOL_BUILD_TOOLS=OFF` when only the library is
wanted.

## Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Each test file under `test/` doubles as a tutorial for the
corresponding header — see `docs/test_conventions.md` for the file conventions
(`example()` / `run_correctness()` per file).

Two-party tests and benchmarks run both parties: `./run <binary> [args]`
launches party 1 and party 2 locally on a fresh `EMP_PORT` per invocation
(`ctest` already wraps the two-party tests in it). The harness fails if either
party fails, terminates the sibling, and imposes a 120-second timeout; set
`EMP_RUN_TIMEOUT` to another positive number of seconds when a longer benchmark
needs it. For a real two-machine run,
set `EMP_PORT` (shared port) on both hosts and `EMP_PEER_IP` (party 1's
address, which party 2 uses to connect) on party 2's host, then start
`<binary> 1 ...` / `<binary> 2 ...` directly.

### Benchmarks

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DEMP_TOOL_BUILD_BENCHMARKS=ON
cmake --build build -j
./build/bench/bench_aes 0.3
./run ./build/bench/bench_netio
```

Benchmarks are separate from `ctest` and live under `bench/`; see
`docs/benchmark_conventions.md`.

### Wire-byte equivalence (test mode)

Setting `EMP_TEST_MODE=1` swaps every randomness source in the
toolkit (`PRG()` default-construction, `ECGroup::rand_scalar`) for
deterministic `(lane, ordinal)`-seeded streams, so two runs of the
same code — single- or multi-threaded — produce byte-identical wire
output. Combined with `TraceIO` (an
`IOChannel` adapter that tees wire bytes to a file), this lets you
verify that an optimization or refactor doesn't change a protocol's
observable behavior:

```bash
EMP_TEST_MODE=1 ./run ./build/your_protocol_test before
# … apply your refactor …
EMP_TEST_MODE=1 ./run ./build/your_protocol_test after
diff before.alice.send after.alice.send   # each direction, checked at its sender
diff before.bob.send   after.bob.send     # both must be empty
```

See [docs/test_mode.md](docs/test_mode.md) for the full design,
determinism contract, and limitations.

## Documentation

| Topic | Doc |
|---|---|
| Typed circuit values, circuit authoring | [docs/circuits.md](docs/circuits.md) |
| Numeric semantics (wrap, shifts, float) | [docs/numeric_semantics.md](docs/numeric_semantics.md) |
| Circuit frontend (compile once, run anywhere) | [docs/frontend.md](docs/frontend.md) |
| Implementing a `BooleanContext` / session backend | [docs/backend.md](docs/backend.md) |
| IOChannel contract (flush, threads, Fiat–Shamir) | [docs/io_channel.md](docs/io_channel.md) |
| API conventions and failure model | [docs/api_conventions.md](docs/api_conventions.md) |
| Old emp-tool API → this tree | [docs/EMP_TRANSLATION.md](docs/EMP_TRANSLATION.md) |
| Floating-point circuit assets | [docs/floating_point_circuits.md](docs/floating_point_circuits.md) |
| Test mode / wire-byte determinism | [docs/test_mode.md](docs/test_mode.md) |
| Test and benchmark conventions | [docs/test_conventions.md](docs/test_conventions.md), [docs/benchmark_conventions.md](docs/benchmark_conventions.md) |

An implementation audit for external reviewers lives in
[audit-report/](audit-report/) (start at `index.html`).

## Security

- Research software. This line has been reviewed by its maintainer
  (see [audit-report/](audit-report/)) and by automated tooling
  (ASan/UBSan CI legs, CodeQL, always-on contract checks, the test
  suite); it has not had an independent security audit.
- No systematic constant-time guarantee. AES-based kernels use CPU AES
  instructions (AES-NI, or NEON via sse2neon); elliptic-curve
  operations call OpenSSL; other kernels are written for throughput,
  and secret-dependent timing has not been audited across the tree.
- `TLSIO` verifies the peer's certificate chain against the configured
  CA (optionally mutually, `require_peer_cert`). It does not check a
  peer identity beyond that: any certificate issued by the configured
  CA authenticates, so the CA must be deployment-private.
- Failures are fail-stop: a violated check terminates the process
  ([docs/api_conventions.md](docs/api_conventions.md)). Test mode
  (`EMP_TEST_MODE=1`) makes all randomness deterministic and is never
  safe for real secrets.
- Report vulnerabilities to wangxiao1254@gmail.com.

## [Acknowledgement, Reference, and Questions](https://github.com/emp-toolkit/emp-readme/blob/master/README.md#citation)

## License

Licensed under the Apache License, Version 2.0 — see [LICENSE](LICENSE).
