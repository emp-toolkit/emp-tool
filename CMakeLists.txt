cmake_minimum_required (VERSION 2.8.12)
project (emptool)
set(NAME "emp-tool")

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

set(sources
emp-tool/emp-tool.cpp
emp-tool/circuits/files/bristol_fashion/aes_128.txt.cpp
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

ENABLE_TESTING()
ADD_SUBDIRECTORY(test)
