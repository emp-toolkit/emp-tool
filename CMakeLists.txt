cmake_minimum_required (VERSION 2.8.12)
project (emptool)
set(NAME "emp-tool")

ADD_CUSTOM_COMMAND(
  OUTPUT emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.cpp
  COMMAND xxd -i emp-tool/circuits/files/bristol_fashion/Keccak_f.txt emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.hex
  COMMAND echo "#include \"emp-tool/circuits/sha3_256.h\"" > emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.cpp
  COMMAND cat emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.hex >> emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.cpp
  COMMENT "Compiling Keccak circuit file to binary")

set(sources
emp-tool/emp-tool.cpp
emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.cpp
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
add_test(to_bool)
add_test(sha3_256)
