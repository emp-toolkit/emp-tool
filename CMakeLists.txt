cmake_minimum_required (VERSION 2.8.12)
project (emptool)
set(NAME "emp-tool")

set(sources
emp-tool/emp-tool.cpp
)

include(cmake/common.cmake)
include(cmake/enable_float.cmake)
include(cmake/enable_rdseed.cmake)

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})


add_library(${NAME} SHARED ${sources})

install(DIRECTORY emp-tool DESTINATION include/)
install(DIRECTORY cmake/ DESTINATION cmake/)
install(TARGETS ${NAME} DESTINATION lib)

# Test cases
macro (add_test _name)
	add_test_with_lib(${_name} "${NAME}")
endmacro()

add_test(prg)
add_test(hash)
add_test(prp)
add_test(netio)
add_test(netio2)
add_test(bit)
add_test(ecc)
add_test(int)
IF(${ENABLE_FLOAT})
add_test(float)
ENDIF(${ENABLE_FLOAT})
add_test(garble)
add_test(gen_circuit)
add_test(mitccrh)
add_test(f2k)
add_test(halfgate)
