cmake_minimum_required (VERSION 2.8.11)
project (emptool)
set(NAME "emp-tool")

include_directories(${CMAKE_SOURCE_DIR}/)
include_directories(${CMAKE_SOURCE_DIR}/io)
include_directories(${CMAKE_SOURCE_DIR}/utils)
include_directories(${CMAKE_SOURCE_DIR}/circuits)
include_directories(${CMAKE_SOURCE_DIR}/gc)

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )

FIND_PACKAGE(OpenSSL)
INCLUDE_DIRECTORIES(${OPENSSL_INCLUDE_DIR})

set (CMAKE_C_FLAGS "-pthread -Wall -march=native -O3 -maes")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg -ggdb")
set (CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++0x")

file(GLOB headers 
"io/*.h"
"circuits/*.h"
"circuits/*.hpp"
"utils/*.h"
"utils/*.hpp"
"gc/*.h"
"gc/*.hpp"
"emp-tool"
)
file(GLOB circuit-files 
"circuits/files/*"
)

file(GLOB libgarble
"garble/*.h"
)

set(sources
gc/backend.cpp
)
add_library(${NAME} SHARED ${sources})

install(FILES emp-tool-config.cmake DESTINATION cmake/)
install(FILES ${headers} DESTINATION include/${NAME})
install(FILES ${libgarble} DESTINATION include/${NAME}/garble)
install(FILES ${circuit-files} DESTINATION include/${NAME}/files)
install(TARGETS ${NAME} DESTINATION lib)
macro (add_test _name)
	add_executable("test_${_name}" "test/${_name}.cpp" ${basics} "gc/backend.cpp")
	target_link_libraries("test_${_name}"  relic  ${OPENSSL_LIBRARIES} gmp ${EMP-TOOL_LIBRARIES}) 
endmacro()
add_test(bit)
add_test(gen_circuit)
