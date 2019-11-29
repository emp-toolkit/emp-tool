cmake_minimum_required (VERSION 2.8.11)
project (emptool)
set(NAME "emp-tool")

include(cmake/common.cmake)
include(cmake/source_of_randomness.cmake)
include(cmake/threading.cmake)


find_package(OpenSSL REQUIRED)
find_package(GMP REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
include_directories(${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${GMP_INCLUDE_DIR})


# Installation
set(sources
emp-tool/emp-tool.cpp
emp-tool/circuits/float32_add.cpp
emp-tool/circuits/float32_cos.cpp
emp-tool/circuits/float32_div.cpp
emp-tool/circuits/float32_eq.cpp
emp-tool/circuits/float32_le.cpp
emp-tool/circuits/float32_leq.cpp
emp-tool/circuits/float32_mul.cpp
emp-tool/circuits/float32_sin.cpp
emp-tool/circuits/float32_sq.cpp
emp-tool/circuits/float32_sqrt.cpp
emp-tool/circuits/float32_sub.cpp
emp-tool/circuits/float32_exp2.cpp
emp-tool/circuits/float32_exp.cpp
emp-tool/circuits/float32_ln.cpp
emp-tool/circuits/float32_log2.cpp
)

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
add_test(com)
add_test(netio)
add_test(bit)
add_test(ecc)
add_test(int)
add_test(float)
add_test(float32)
add_test(garble)
add_test(gen_circuit)
add_test(mitccrh)
