#Testing macro
macro (add_test_executable_with_lib _name libs)
	add_executable(test_${_name} "${_name}.cpp")
	target_link_libraries(test_${_name} ${OPENSSL_LIBRARIES} ${libs})
endmacro()

macro (add_test_case _name)
	add_test_executable_with_lib(${_name} "emp-tool")
  	add_test(NAME ${_name} COMMAND "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test_${_name}" WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/")
endmacro()

macro (add_test_case_with_run _name)
	add_test_executable_with_lib(${_name} "emp-tool")
	add_test(NAME ${_name} COMMAND "${PROJECT_SOURCE_DIR}/run" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test_${_name}" WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/")

endmacro()


# Test cases — one file per emp-tool/utils/*.h header (see CLAUDE.md).
# Each test file is a tutorial: example() / run_correctness() / bench(sec).

# utils/* tests
add_test_case(aes)
add_test_case(block)
add_test_case(ccrh)
add_test_case(crh)
add_test_case(f2k)
add_test_case(hash)
add_test_case(mitccrh)
add_test_case(prg)
add_test_case(prp)
add_test_case(tccrh)
add_test_case(utils)

# group/EC
add_test_case(ecc)

# circuits / IO / higher-level — not utils/* but kept here for convenience
add_test_case_with_run(netio)
add_test_case_with_run(netio2)
add_test_case(bit)
add_test_case(int)
add_test_case(float)
add_test_case_with_run(garble)
add_test_case(gen_circuit)
add_test_case(halfgate)

IF(${CRYPTO_IN_CIRCUIT})
add_test_case(sha3_256)
add_test_case(aes_128_ctr)
ENDIF(${CRYPTO_IN_CIRCUIT})