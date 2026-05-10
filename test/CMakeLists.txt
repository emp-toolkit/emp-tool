# Test cases — one file per crypto/core/io/group header (see CLAUDE.md).
# Each test file is a tutorial: example() / run_correctness() / bench(sec).

function(add_test_case name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE emp-tool::emp-tool)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ${name}
             COMMAND ${name}
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

function(add_test_case_with_run name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE emp-tool::emp-tool)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ${name}
             COMMAND ${PROJECT_SOURCE_DIR}/run $<TARGET_FILE:${name}>
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

# utils/* tests
add_test_case(test_aes)
add_test_case(test_block)
add_test_case(test_ccrh)
add_test_case(test_f2k)
add_test_case(test_hash)
add_test_case(test_mitccrh)
add_test_case(test_prg)
add_test_case(test_prp)
add_test_case(test_utils)

# group/EC
add_test_case(test_ecc)

# circuits / IO / higher-level — not utils/* but kept here for convenience
add_test_case_with_run(test_netio)
add_test_case_with_run(test_tlsio)
add_test_case(test_bit)
add_test_case(test_bitvec)
add_test_case(test_uint)
add_test_case(test_int)
add_test_case(test_float)
add_test_case_with_run(test_garble)
add_test_case(test_gen_circuit)
add_test_case(test_halfgate)
add_test_case(test_aes_circuit)
add_test_case(test_aes_128_ctr)
add_test_case(test_sha3_circuit)
add_test_case(test_sha3_256)
