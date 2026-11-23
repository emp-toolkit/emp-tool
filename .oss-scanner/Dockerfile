# Build environment for OSS Scanner. The build runs with network access; the
# scan that follows runs offline, so every package the tests and any
# reproducer need is installed here.
#
# Two builds of the same checkout:
#   /src/build      GCC, RelWithDebInfo, the default configuration
#   /src/build-san  Clang, RelWithDebInfo, ASan + UBSan (as in CI)
#
# EMP_TOOL_NATIVE_ARCH=OFF in both: the image is built on one machine and
# scanned on another, and -march=native binaries can SIGILL on a CPU with a
# smaller ISA. The portable baseline is AES-NI + PCLMUL + SSE4.2 on x86-64 and
# armv8-a+crypto on arm64.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates build-essential clang libclang-rt-18-dev llvm cmake \
      ninja-build pkg-config git python3 libssl-dev gdb \
    && rm -rf /var/lib/apt/lists/*

# Scanner contract: the checkout lives at /src.
COPY . /src
WORKDIR /src

RUN CC=gcc CXX=g++ cmake -S /src -B /src/build -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DEMP_TOOL_NATIVE_ARCH=OFF \
      -DCMAKE_C_FLAGS="-fno-omit-frame-pointer" \
      -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer" \
    && cmake --build /src/build -j"$(nproc)"

RUN CC=clang CXX=clang++ cmake -S /src -B /src/build-san -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DEMP_TOOL_NATIVE_ARCH=OFF \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
      -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
    && cmake --build /src/build-san -j"$(nproc)"

# Smoke-run both test suites; a failure is logged but does not fail the image.
# Each two-party test has its own port block, so ctest -j is safe.
RUN (ctest --test-dir /src/build --output-on-failure -j"$(nproc)" --timeout 300 \
      || echo "WARNING: ctest (build) reported failures") \
    && (ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
        UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        ctest --test-dir /src/build-san --output-on-failure -j"$(nproc)" --timeout 300 \
      || echo "WARNING: ctest (build-san) reported failures")
