cmake_minimum_required (VERSION 2.8.11)
project (emptool)
set(NAME "emp-tool")

if(POLICY CMP0042)
  cmake_policy(SET CMP0042 NEW) # use rpath on macOS
endif()

include_directories(${CMAKE_SOURCE_DIR}/)
include_directories(${CMAKE_SOURCE_DIR}/io)
include_directories(${CMAKE_SOURCE_DIR}/utils)
include_directories(${CMAKE_SOURCE_DIR}/circuits)
include_directories(${CMAKE_SOURCE_DIR}/gc)
include_directories(${CMAKE_SOURCE_DIR}/plain)

if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE Release)
endif(NOT CMAKE_BUILD_TYPE)
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_SOURCE_DIR})

find_package(OpenSSL REQUIRED)
find_package(relic REQUIRED)
include_directories(${RELIC_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})

set (CMAKE_C_FLAGS "-pthread -Wall -march=native -O1 -maes")
set (CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")


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

install(FILES emp-tool-config.cmake relic-config.cmake DESTINATION cmake/)
install(FILES ${headers} DESTINATION include/${NAME})
install(FILES ${libgarble} DESTINATION include/${NAME}/garble)
install(FILES ${circuit-files} DESTINATION include/${NAME}/files)
install(TARGETS ${NAME} DESTINATION lib)

macro (add_test _name)
	add_executable(${_name} "test/${_name}.cpp" ${sources})
	target_link_libraries(${_name}  ${RELIC_LIBRARIES} ${OPENSSL_LIBRARIES} gmp) 
endmacro()

add_test(prg)
add_test(hash)
add_test(prp)
add_test(com)
add_test(netio)
add_test(bit)
add_test(int)
add_test(float)
add_test(garble)
#add_test(gen_circuit)
