# Vendored BLAKE3

Upstream: https://github.com/BLAKE3-team/BLAKE3
Version:  1.8.5
Commit:   8aa5145039b972ba30e98e788752d37d14568824
License:  dual-licensed CC0-1.0 OR Apache-2.0 (with the Apache-2.0-w/-LLVM-exception
          variant); see `LICENSE_CC0`, `LICENSE_A2`, `LICENSE_A2LLVM`.

## Why it's here

emp-tool's `Hash` (`runtime/crypto/hash.h`) supports a swappable backend policy.
The default backend is OpenSSL SHA-256; a build may opt into the BLAKE3 backend
(`HashT<HashOption::blake3>`; configure with `-DEMP_WITH_BLAKE3=ON`). BLAKE3 is
~3-4x faster per byte than SHA-NI and, since preprocessing hashing sits on the
serial critical path, that translates into a measured end-to-end speedup for
the maliciously-secure garbling backends.

## What's included

Only the files needed to build the C library from portable + SIMD intrinsics
(no assembly, no Rust, no TBB multithreading, no examples/tests):

    blake3.h            public API
    blake3.c            blake3_dispatch.c  blake3_portable.c   blake3_impl.h
    blake3_sse2.c  blake3_sse41.c  blake3_avx2.c  blake3_avx512.c   (x86 SIMD)
    blake3_neon.c                                                   (ARM NEON)

`blake3_dispatch.c` selects the fastest available backend at run time via CPUID,
so the same objects are portable across x86 microarchitectures.

## Build integration

There is no separate build here: when `-DEMP_WITH_BLAKE3=ON` (implied by
`-DEMP_HASH_BACKEND=blake3`), the top-level `emp-tool/CMakeLists.txt` compiles
these sources straight into `libemp-tool` with the per-file ISA flags
(`-msse2 / -msse4.1 / -mavx2 / -mavx512f -mavx512vl`, or NEON on ARM) and adds
this directory as a PUBLIC include path plus the `EMP_WITH_BLAKE3` define. The
objects therefore travel inside `libemp-tool` and the usage requirements
propagate through emp-tool's exported target, so consumers pick up BLAKE3
with no extra dependency to resolve.

## Updating

Re-copy the `c/` sources for the desired release, refresh the version/commit
above, and diff `blake3.h` for API changes (we use only the incremental
`blake3_hasher_{init,update,finalize,reset}` calls with the default 32-byte
output — a stable, long-standing API surface).
