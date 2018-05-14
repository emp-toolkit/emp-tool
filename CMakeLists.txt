cmake_minimum_required (VERSION 2.8.11)
project (emptool)
set(NAME "emp-tool")
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

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
find_package(GMP REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
include_directories(${RELIC_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${GMP_INCLUDE_DIR})

set (CMAKE_C_FLAGS "-pthread -Wall -march=native -O3 -maes -mrdseed")
set (CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

OPTION(USE_RANDOM_DEVICE "Option description" OFF)

unset(RDSEED_COMPILE_RESULT CACHE)
unset(RDSEED_RUN_RESULT CACHE)
try_run(RDSEED_RUN_RESULT RDSEED_COMPILE_RESULT ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/compiler_test/rdseedtest.c CMAKE_FLAGS ${CMAKE_C_FLAGS})
IF(NOT ${RDSEED_COMPILE_RESULT})
        set(USE_RANDOM_DEVICE ON)
ELSE(NOT ${RDSEED_COMPILE_RESULT})
	string(COMPARE EQUAL "${RDSEED_RUN_RESULT}" "0" RDSEED_RUN_SUCCESS)
        IF(NOT ${RDSEED_RUN_SUCCESS})
                set(USE_RANDOM_DEVICE ON)
        ENDIF(NOT ${RDSEED_RUN_SUCCESS})
ENDIF(NOT ${RDSEED_COMPILE_RESULT})

IF(USE_RANDOM_DEVICE)
	ADD_DEFINITIONS(-DEMP_USE_RANDOM_DEVICE)
	message("Use Random Device: on")
ENDIF(USE_RANDOM_DEVICE)
OPTION(THREADING "Option description" OFF)
IF(THREADING)
	ADD_DEFINITIONS(-DTHREADING)
	message("Multi Threading: on")
ENDIF(THREADING)

set(sources
emp-tool/emp-tool.cpp
)

add_library(${NAME} SHARED ${sources})

install(FILES cmake/emp-tool-config.cmake cmake/relic-config.cmake DESTINATION cmake/)
install(DIRECTORY emp-tool DESTINATION include/)
install(TARGETS ${NAME} DESTINATION lib)

macro (add_test _name)
	add_executable(${_name} "test/${_name}.cpp" ${sources})
	target_link_libraries(${_name}  ${RELIC_LIBRARIES} ${OPENSSL_LIBRARIES} ${Boost_LIBRARIES} ${GMP_LIBRARIES}) 
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
add_test(gen_circuit)
