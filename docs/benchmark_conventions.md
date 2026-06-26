# Benchmark conventions

How `bench/*.cpp` files are structured. Read this when adding or
modifying throughput benchmarks.

Benchmarks are built only when `EMP_TOOL_BUILD_BENCHMARKS=ON`; they are
not registered with `ctest`. Two-party benchmarks such as NetIO and TLSIO
are run through `./run`; single-process benchmarks (e.g. the half-gate
garbling bench, which garbles and evaluates locally) run directly.

## File layout

Benchmark files are named `bench_<component>.cpp` and build to
`build/bench/bench_<component>`.

Each benchmark accepts per-row seconds from `argv[1]` and defaults to
`0.2` seconds unless the benchmark has a strong reason to do otherwise.

## Measurement rules

- **Single-shot ops** (e.g. `gfmul`, `mul128`, `reduce`, AES
  `set_key`): chain output back into input so each call has a real
  serial dependency on the previous. Otherwise the compiler can hoist
  the loop-invariant call out and `cy/op` reflects only the asm
  clobber. Report `Mops` and `cy/op @3GHz` (notional 3 GHz
  normalization, not a hardware claim).
- **Vector ops over a size parameter**: sweep N over a representative
  range (e.g. `{16, 64, 256, 1024, 4096, 16384, 65536}`) so the
  reader can see where the routine becomes bandwidth-bound vs
  compute-bound. Report `GiB/s` of input touched and `cy/blk @3GHz`
  or `cy/B @3GHz`.
- **Fixed-size ops** (e.g. `packing(N=128)`): single row, report
  `Mops` and `cy/op @3GHz`.
- Use `__attribute__((target("sse2")))` on x86 where the timing
  primitives need SSE.

## Harness skeleton

```cpp
template <typename Fn>
static double run_for(double seconds, Fn &&fn, void *clob) {
    for (int i = 0; i < 32; ++i) {
        fn();
        asm volatile("" : "+m"(*(char *)clob) : : "memory");
    }
    int64_t iters = 64;
    while (true) {
        auto a = clk::now();
        for (int64_t i = 0; i < iters; ++i) {
            fn();
            asm volatile("" : "+m"(*(char *)clob) : : "memory");
        }
        double el = chrono::duration<double>(clk::now() - a).count();
        if (el >= seconds) return double(iters) / el;
        iters *= 2;
    }
}
```

The `clob` pointer must be alive through the iteration; clobbering it
via inline asm prevents dead-code elimination of the bench body.

## Running

```bash
cmake -B build -DEMP_TOOL_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bench/bench_aes 0.3
./run ./build/bench/bench_netio
```
