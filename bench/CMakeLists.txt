# Benchmarks are built separately from tests and are not registered with ctest.
# Run local two-party benchmarks via ./run, for example:
#   ./run ./build/bench/bench_netio

function(add_bench name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE emp-tool::emp-tool)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench")
endfunction()

add_bench(bench_aes)
add_bench(bench_block)
add_bench(bench_halfgate)
add_bench(bench_ccrh)
add_bench(bench_f2k)
add_bench(bench_hash)
add_bench(bench_mitccrh)
add_bench(bench_prg)
add_bench(bench_prp)
add_bench(bench_rijndael256)
add_bench(bench_utils)

add_bench(bench_netio)
add_bench(bench_tlsio)
