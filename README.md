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
> - **New projects, or able to track a moving API: use the development branch**
>   (this branch). Its CMake package metadata is already `1.0.0` for
>   local development, but the API is still pre-alpha and not frozen.
>   Faster, cleaner APIs (test-mode determinism, trace I/O, refactored
>   AES/PRG/group), but headers and names may still move before the
>   first tagged alpha/release.

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
- A C++20 compiler (Clang ≥ 12, GCC ≥ 10, AppleClang 14+)
- OpenSSL ≥ 3.0
- pthreads
- x86_64 with AES-NI + PCLMULQDQ + SSE4.2, **or** arm64 with `armv8-a+crypto+crc`. The default build uses `-march=native` and pulls in VAES, VPCLMULQDQ, AVX-512 etc. wherever the host CPU has them; pass `-DEMP_TOOL_NATIVE_ARCH=OFF` for a portable binary tied only to the baseline above.

## Build and install

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build           # respects CMAKE_INSTALL_PREFIX
```

The default build is tuned for performance: `Release`, `-O3
-funroll-loops`, and `-march=native` so VAES / VPCLMULQDQ / AVX-512 etc.
are used wherever the host CPU supports them. **Binaries built this way
are tied to the build machine's CPU** — they will SIGILL on a CPU
missing any instruction the build host had. To produce a portable
binary that runs on any AES-NI + PCLMUL + SSE4.2 (x86_64) or
`armv8-a+crypto+crc` (arm64) machine, pass
`-DEMP_TOOL_NATIVE_ARCH=OFF`.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `EMP_TOOL_NATIVE_ARCH` | `ON` | Build with `-march=native`. Best performance, host-CPU-locked binary. Set `OFF` for portable binaries. |
| `EMP_TOOL_BUILD_TESTS` | `ON` when top-level | Build the test suite under `test/`. |
| `EMP_TOOL_BUILD_BENCHMARKS` | `OFF` | Build throughput benchmarks under `bench/`; not registered with `ctest`. |
| `EMP_TOOL_INSTALL` | `ON` when top-level | Generate install + export rules. |

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

The canonical circuit value layer is the context-bound typed values in
`circuits/typed.h`: `Bit_T<Ctx>`, `UInt_T<Ctx,N>`, `Int_T<Ctx,N>`,
`Float_T<Ctx,W>`, and `BitVec_T<Ctx,N>`, each templated on a `BooleanContext`
`Ctx`. There is no global backend — every value carries its context and issues
value-return gates on it.

The numeric layer makes signedness explicit: `UInt_T<Ctx,N>` wraps mod 2^N
matching `uint{N}_t`, `Int_T<Ctx,N>` is two's-complement matching `int{N}_t` on
hardware (C signed-overflow UB is sidestepped — emp-tool wraps
deterministically). `Float_T<Ctx,W>` is IEEE binary{16,32,64}. Comparisons
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
templated batched `H<n>(out, in)` that the compiler unrolls (best up to
n ≤ 16, beyond which register spills hurt throughput), and a runtime
`Hn(out, in, n)` for large batches. MITCCRH has a different shape — see
`crypto/mitccrh.h`. `CCRH` is the single correlation-robust hash: its `sigma`
preprocessing costs roughly half a cycle per block in bulk and rules out a
footgun class of misuse where a plain CRH leaves `H(in)` and `H(in ⊕ Δ)`
correlated.

### Hash (SHA-256)

```cpp
Hash hash;
char data[1024];
char dig[Hash::DIGEST_SIZE];                     // 32 bytes

hash.put(data, sizeof(data));
hash.digest(dig);                                // resets after digesting
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

`ECGroup` wraps an OpenSSL `EC_GROUP` + `BN_CTX`. Default curve is
P-256; pass any OpenSSL `NID_*` to the constructor to switch.
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
NetIO io(party == ALICE ? nullptr : "127.0.0.1", 12345);
io.send_data(buf, n);                            // buffered
io.flush();                                      // drain outbound
io.recv_data(buf, n);                            // blocks until n bytes arrive
```

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

`UInt_T` wraps mod 2^N, `Int_T` is two's-complement, `Float_T` is IEEE
binary{16,32,64}, and comparisons return `Bit_T<ClearCtx>`. The same typed
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
using namespace emp;
namespace cf = emp::frontend;

auto add  = [](auto a, auto b){ return a + b; };               // pure circuit (implicit ctx)
auto circ = cf::compile<rec::UInt<32>, rec::UInt<32>>(add);    // record ONCE -> Circuit

ClearCtx cx;                                                   // ... then run on any context
auto x = UInt_T<ClearCtx,32>::constant(cx, 7);
auto y = UInt_T<ClearCtx,32>::constant(cx, 5);
auto z = cf::run(cx, circ, x, y);                             // replay -> UInt_T<ClearCtx,32> (== 12)
```

The same `circ` runs identically on `ClearCtx`, the garbled `SH2PCCtx`, and
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
rejects malformed files. Floating-point `.empbc` assets ship in
`emp-tool/ir/files/`; see
[docs/floating_point_circuits.md](docs/floating_point_circuits.md) for the
asset format and regeneration notes. You can also `compile` your own pure
circuit function (above) or capture a recorded program and load it through this
API.

```cpp
#include <emp-tool/ir/context/context.h>   // execute_program, ClearCtx
#include <emp-tool/ir/empbc.h>     // load_empbc_file
using namespace emp;
using namespace emp::circuit;

BooleanProgram program = load_empbc_file("my_circuit.empbc");

ClearCtx ctx;                                            // any BooleanContext
std::vector<ClearCtx::Wire> inputs(program.num_inputs);
// ... fill inputs (the leading wires) ...
std::vector<ClearCtx::Wire> out =
    execute_program(ctx, program,
                    std::span<const ClearCtx::Wire>(inputs.data(), inputs.size()));
```

`execute_program(ctx, program, inputs)` walks the gate list issuing the
context's value-return gate ops, so the same loaded program runs on `ClearCtx`,
the garbled `SH2PCCtx`, or any other context unchanged. A bulk/round-sensitive
context can consume the AND-depth schedule instead (`make_scheduled_plan` +
`scheduled_execute_program`).

## Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Each test file under `test/` doubles as a tutorial for the
corresponding header — see `docs/test_conventions.md` for the file conventions
(`example()` / `run_correctness()` per file).

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
toolkit (`PRG()` default-construction, `ECGroup::rand_scalar`) for a
deterministic counter-derived stream so two runs of the same code
produce byte-identical wire output. Combined with `TraceIO` (an
`IOChannel` adapter that tees wire bytes to a file), this lets you
verify that an optimization or refactor doesn't change a protocol's
observable behavior:

```bash
EMP_TEST_MODE=1 ./run ./build/your_protocol_test before
# … apply your refactor …
EMP_TEST_MODE=1 ./run ./build/your_protocol_test after
diff before.alice.send after.alice.send   # must be empty
diff before.alice.recv after.alice.recv   # must be empty
```

See [docs/test_mode.md](docs/test_mode.md) for the full design,
determinism contract, and limitations.

## [Acknowledgement, Reference, and Questions](https://github.com/emp-toolkit/emp-readme/blob/master/README.md#citation)

## License

Licensed under the Apache License, Version 2.0 — see [LICENSE](LICENSE).
