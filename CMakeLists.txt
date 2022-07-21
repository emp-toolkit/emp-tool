cmake_minimum_required (VERSION 3.0)
project (emptool)
set(NAME "emp-tool")

set(CMAKE_FOLDER ./)
include(${CMAKE_FOLDER}/cmake/emp-base.cmake)

IF(${CRYPTO_IN_CIRCUIT})
ADD_CUSTOM_COMMAND(
  OUTPUT ${CMAKE_SOURCE_DIR}/emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.cpp
  COMMAND xxd -i emp-tool/circuits/files/bristol_fashion/Keccak_f.txt emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.hex
  COMMAND echo "\\#include \\\"emp-tool/circuits/sha3_256.h\\\"" > emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.cpp
  COMMAND cat emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.hex >> emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.cpp
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Compiling Keccak circuit file to binary")

ADD_CUSTOM_COMMAND(
  OUTPUT ${CMAKE_SOURCE_DIR}/emp-tool/circuits/files/bristol_fashion/aes_128.txt.cpp
  COMMAND xxd -i emp-tool/circuits/files/bristol_fashion/aes_128.txt emp-tool/circuits/files/bristol_fashion/aes_128.txt.hex
  COMMAND echo "\\#include \\\"emp-tool/circuits/aes_128_ctr.h\\\"" > emp-tool/circuits/files/bristol_fashion/aes_128.txt.cpp
  COMMAND cat emp-tool/circuits/files/bristol_fashion/aes_128.txt.hex >> emp-tool/circuits/files/bristol_fashion/aes_128.txt.cpp
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Compiling aes_128 circuit file to binary")
ENDIF(${CRYPTO_IN_CIRCUIT})

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

IF(${CRYPTO_IN_CIRCUIT})
set(sources
${sources}
emp-tool/circuits/files/bristol_fashion/aes_128.txt.cpp
emp-tool/circuits/files/bristol_fashion/Keccak_f.txt.cpp
)
ENDIF(${CRYPTO_IN_CIRCUIT})



find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})


add_library(${NAME} SHARED ${sources})

install(DIRECTORY emp-tool DESTINATION include/)
install(DIRECTORY cmake/ DESTINATION cmake/)
install(TARGETS ${NAME} DESTINATION lib)

ENABLE_TESTING()
ADD_SUBDIRECTORY(test)
