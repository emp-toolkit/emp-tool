# Test cases — one file per crypto/core/io/group header (see CLAUDE.md).
# Each test file is a tutorial: example() / run_correctness() / bench(sec).

function(add_test_case name)
    add_executable(test_${name} ${name}.cpp)
    target_link_libraries(test_${name} PRIVATE emp-tool::emp-tool)
    add_test(NAME ${name}
             COMMAND test_${name}
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

function(add_test_case_with_run name)
    add_executable(test_${name} ${name}.cpp)
    target_link_libraries(test_${name} PRIVATE emp-tool::emp-tool)
    add_test(NAME ${name}
             COMMAND ${PROJECT_SOURCE_DIR}/run $<TARGET_FILE:test_${name}>
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

# utils/* tests
add_test_case(aes)
add_test_case(block)
add_test_case(ccrh)
add_test_case(f2k)
add_test_case(hash)
add_test_case(mitccrh)
add_test_case(prg)
add_test_case(prp)
add_test_case(utils)

# group/EC
add_test_case(ecc)

# circuits / IO / higher-level — not utils/* but kept here for convenience
add_test_case_with_run(netio)
add_test_case(bit)
add_test_case(bitvec)
add_test_case(uint)
add_test_case(int)
add_test_case(float)
add_test_case_with_run(garble)
add_test_case(gen_circuit)
add_test_case(halfgate)
add_test_case(aes_circuit)
add_test_case(aes_128_ctr)
add_test_case(sha3_circuit)
add_test_case(sha3_256)
