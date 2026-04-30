# emp-tool

![arm](https://github.com/emp-toolkit/emp-tool/workflows/arm/badge.svg)
![x86](https://github.com/emp-toolkit/emp-tool/workflows/x86/badge.svg)
[![CodeQL](https://github.com/emp-toolkit/emp-tool/actions/workflows/codeql.yml/badge.svg)](https://github.com/emp-toolkit/emp-tool/actions/workflows/codeql.yml)

<img src="https://raw.githubusercontent.com/emp-toolkit/emp-readme/master/art/logo-full.jpg" width=300px/>

Foundational primitives for the emp-toolkit family: SIMD `block` types,
fast AES / PRG / PRP / hash / GF(2^128) kernels, OpenSSL-backed elliptic
curve ops, IO channels, a templated boolean-circuit frontend
(`Bit_T<Wire>` / `BitVec_T<Wire>` / `UnsignedInt_T<Wire>` /
`SignedInt_T<Wire>` / `Float_T<Wire>`), and pluggable execution backends
(plaintext circuit printer, half-gate garbling, privacy-free garbling).

## Requirements

- CMake ≥ 3.21
- A C++17 compiler (Clang ≥ 12, GCC ≥ 9, AppleClang 14+)
- OpenSSL (≥ 1.1; tested against 3.x)
- pthreads
- x86_64 with AES-NI + PCLMULQDQ + SSSE3, **or** arm64 with `armv8-a+crypto+crc`. The default build uses `-march=native` and pulls in VAES, VPCLMULQDQ, AVX-512 etc. wherever the host CPU has them; pass `-DEMP_TOOL_NATIVE_ARCH=OFF` for a portable binary tied only to the baseline above.

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
binary that runs on any AES-NI + PCLMUL + SSSE3 (x86_64) or
`armv8-a+crypto+crc` (arm64) machine, pass
`-DEMP_TOOL_NATIVE_ARCH=OFF`.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `EMP_TOOL_NATIVE_ARCH` | `ON` | Build with `-march=native`. Best performance, host-CPU-locked binary. Set `OFF` for portable binaries. |
| `EMP_TOOL_BUILD_TESTS` | `ON` when top-level | Build the test suite under `test/`. |
| `EMP_TOOL_INSTALL` | `ON` when top-level | Generate install + export rules. |
| `EMP_TOOL_THREADING` | `OFF` | Make the global `Backend* backend` pointer thread-local. Required if multiple threads run circuits concurrently against different backends. |
| `EMP_TOOL_CRYPTO_IN_CIRCUIT` | `OFF` | Embed the AES-128 and Keccak-f Bristol circuits as static blobs (needed by `AES_128_CTR_Calculator` / `SHA3_256_Calculator` and their tests). Requires `xxd`. |

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

A single header pulls everything in:

```cpp
#include <emp-tool/emp-tool.h>
using namespace emp;
```

## Layout

```
emp-tool/
├── core/         block, constants, utils
├── crypto/       PRG, PRP, AES, Hash, CCRH, MITCCRH, f2k
├── group/        BigInt + elliptic-curve Group/Point (OpenSSL-backed)
├── io/           IOChannel, NetIO, MemIO
├── execution/    Backend interface, ClearBackend, HalfGate*, PrivacyFree*
├── circuits/     Bit, BitVec, UnsignedInt, SignedInt, Float (all templated on Wire), BristolFormat/Fashion
└── third_party/  ThreadPool, sse2neon
```

`circuits/` is templated on the wire type; `emp-tool.h` provides the
default aliases (`Bit`, `BitVec`, `UnsignedInt`, `SignedInt`, `Float`)
all over `block`, so most consumers never have to think about it.

The numeric layer makes signedness explicit: `UnsignedInt` wraps mod
2^N matching `uint{N}_t`, `SignedInt` is two's-complement matching
`int{N}_t` on hardware (C signed-overflow UB is sidestepped — emp-tool
wraps deterministically), and `BitVec` carries no arithmetic at all
(just bitwise / shifts / slice / concat). `UnsignedInt` and `SignedInt`
inherit from `BitVec` so they pick up the structural ops; conversion
between signed and unsigned is an explicit `.as_signed()` /
`.as_unsigned()` bit-cast (no gates).

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
| `MITCCRH` | multi-instance tweakable CCRH (used by half-gate garbling) |

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
`crypto/mitccrh.h`. The plain non-circular CRH used to be a separate
class but has been removed; CCRH supersedes it (its `sigma` preprocessing
costs roughly half a cycle per block in bulk and rules out a footgun
class of misuse where `H(in)` and `H(in ⊕ Δ)` are correlated).

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

### Network IO

```cpp
NetIO io(party == ALICE ? nullptr : "127.0.0.1", 12345);
io.send_data(buf, n);                            // buffered
io.flush();                                      // drain outbound
io.recv_data(buf, n);                            // blocks until n bytes arrive
```

`MemIO` has the same interface and is useful for benchmarks or unit
tests that don't need a socket.

### Plaintext circuit evaluation

The simplest backend evaluates `Bit` / `BitVec` / `UnsignedInt` /
`SignedInt` / `Float` in cleartext (and can optionally dump a
Bristol-style file of the circuit it executed):

```cpp
setup_clear_backend();                           // installs ClearBackend

SignedInt a(32, 7,  PUBLIC);
SignedInt b(32, 35, PUBLIC);
SignedInt c = a * b + SignedInt(32, 1, PUBLIC);
std::cout << c.reveal<int32_t>() << "\n";        // 246

// Wrap on overflow is well-defined and matches int32_t / uint32_t hardware:
UnsignedInt big(32, UINT32_MAX, PUBLIC);
std::cout << (big + UnsignedInt(32, 1u, PUBLIC)).reveal<uint32_t>() << "\n"; // 0

finalize_clear_backend();
```

To dump the executed circuit as a Bristol-format file, pass a filename
to `setup_clear_backend("circuit.txt")`.

### Half-gate garbling

`HalfGateGen` (Alice) and `HalfGateEva` (Bob) are template-parameterized
on the IO channel. Input feeding and output revealing require OT and
live in **emp-ot**; the bare classes in emp-tool are the gate-evaluation
core, suitable for benchmarking garbling speed:

```cpp
NetIO io(party == ALICE ? nullptr : "127.0.0.1", 12345);
backend = (party == ALICE) ? new HalfGateGen<NetIO>(&io)
                           : new HalfGateEva<NetIO>(&io);

// ... circuit using Bit / BitVec / UnsignedInt / SignedInt / Float ...

delete backend;
backend = nullptr;
```

For end-to-end 2PC use **emp-sh2pc** (semi-honest) or **emp-ag2pc**
(malicious) which compose emp-tool and emp-ot.

### Pre-built circuits (Bristol format)

```cpp
BristolFashion aes("emp-tool/circuits/files/bristol_fashion/aes_128.txt");

Bit input[256];                                  // 2 × 128-bit input groups
Bit output[128];
// ... feed inputs via your protocol ...
aes.compute(output, input);
```

`BristolFormat` is the older two-input-array variant; `BristolFashion`
is the unified-input form used by most circuits in
`emp-tool/circuits/files/bristol_fashion/`.

### Custom wire type

If you implement your own backend with a non-`block` wire (e.g. an
arithmetic share, a long bytestring), instantiate the circuit templates
on it directly. Skip `<emp-tool/emp-tool.h>` (which would pull in the
default `block` aliases) and include only what you need:

```cpp
#include <emp-tool/execution/backend.h>
#include <emp-tool/circuits/bit.h>
#include <emp-tool/circuits/bitvec.h>
#include <emp-tool/circuits/unsigned_int.h>
#include <emp-tool/circuits/signed_int.h>

struct MyWire { /* ... */ };
class MyBackend : public Backend { /* override wire_bytes/and_gate/... */ };

backend = new MyBackend(/* args */);

using MyBit         = Bit_T<MyWire>;
using MyBitVec      = BitVec_T<MyWire>;
using MyUnsignedInt = UnsignedInt_T<MyWire>;
using MySignedInt   = SignedInt_T<MyWire>;
```

The class definitions in `circuits/` carry no `block` of their own; the
only place emp-tool commits to `block` as the wire is the alias block at
the bottom of `emp-tool/emp-tool.h`.

## Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Add `-DEMP_TOOL_CRYPTO_IN_CIRCUIT=ON` to also build the
`aes_128_ctr` and `sha3_256` in-circuit tests. Each test file under
`test/` doubles as a tutorial for the corresponding header — see
`CLAUDE.md` for the file conventions (`example()` / `run_correctness()`
/ `bench(double sec)` per file).

## [Acknowledgement, Reference, and Questions](https://github.com/emp-toolkit/emp-readme/blob/master/README.md#citation)

## License

Dual-licensed under either of:

- MIT License ([LICENSE-MIT](LICENSE-MIT))
- Apache License 2.0 ([LICENSE-APACHE](LICENSE-APACHE))

You may choose either license when using this software.
