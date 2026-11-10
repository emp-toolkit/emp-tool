# Third-party notices

emp-tool contains or distributes the following third-party material.

- **BLAKE3 1.8.5** — dual-licensed CC0-1.0 or Apache-2.0, including the
  Apache-2.0 LLVM exception variant. The complete license texts and vendoring
  metadata are in `third_party/blake3/` and are installed when BLAKE3 is built.
- **sse2neon** — MIT License. The complete copyright and permission notice is
  retained at the top of the installed `emp-tool/third_party/sse2neon.h`.
- **progschj ThreadPool** — zlib-style license. The complete notice is retained
  at the top of the installed `emp-tool/third_party/ThreadPool.h`; emp-tool's
  deterministic test-lane integration, the exception-free enqueue error path
  (`emp::expecting` instead of `throw`) and the move into `namespace emp` are
  marked as alterations in that file.
- **Berkeley SoftFloat 3e** — BSD 3-Clause. Earlier generated circuit assets
  were derived from it; the complete notice is kept beside the `.empbc` assets
  in `emp-tool/ir/files/README.md`.
- **AES S-box circuit** — the straight-line program
  `aes-sbox-a28-ad4-g131-gd32-xx103-47` from the NIST Circuit Complexity
  repository (github.com/usnistgov/Circuits), contributed by Milad Nasr, used
  in `emp-tool/circuits/crypto/aes128.h` and the `aes128.empbc` asset. The
  attribution is kept in `aes128.h`.

This file is informational; the license texts named above govern their
respective components.
