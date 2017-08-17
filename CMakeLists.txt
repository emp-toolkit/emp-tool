cmake_minimum_required (VERSION 2.8.11)
project (emptool)
set(NAME "emp-tool")

if(POLICY CMP0042)
  cmake_policy(SET CMP0042 NEW) # use rpath on macOS
endif()

include_directories(${CMAKE_SOURCE_DIR})

if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE Release)
endif(NOT CMAKE_BUILD_TYPE)
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_SOURCE_DIR}/cmake)

find_package(OpenSSL REQUIRED)
find_package(relic REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
include_directories(${RELIC_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS})

set (CMAKE_C_FLAGS "-pthread -Wall -march=native -O3 -maes -mrdseed")
set (CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
OPTION(USE_RANDOM_DEVICE "Option description" OFF)
IF(USE_RANDOM_DEVICE)
    ADD_DEFINITIONS(-DEMP_USE_RANDOM_DEVICE)
ENDIF(USE_RANDOM_DEVICE)


set(sources
emp-tool/emp-tool.cpp
)

add_library(${NAME} SHARED ${sources})

install(FILES cmake/emp-tool-config.cmake cmake/relic-config.cmake DESTINATION cmake/)
install(DIRECTORY emp-tool DESTINATION include/)
install(TARGETS ${NAME} DESTINATION lib)

macro (add_test _name)
	add_executable(${_name} "test/${_name}.cpp" ${sources})
	target_link_libraries(${_name}  ${RELIC_LIBRARIES} ${OPENSSL_LIBRARIES} ${Boost_LIBRARIES} gmp) 
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
