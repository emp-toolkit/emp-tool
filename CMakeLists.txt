cmake_minimum_required (VERSION 2.8.11)
project (emptool)
set(NAME "emp-tool")


set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} cmake/ ${CMAKE_SOURCE_DIR})
FIND_PACKAGE(cryptoTools REQUIRED)
FIND_PACKAGE(relic       REQUIRED)
FIND_PACKAGE(gmp         REQUIRED)

include_directories(
	${CMAKE_SOURCE_DIR}/.. 
	${cryptoTools_INCLUDE_DIRS} 
	${relic_INCLUDE_DIRS}
	${GMP_INCLUDE_DIRS})
	
set(CMAKE_C_FLAGS "-pthread -Wall -march=native -O3 -maes")

set (CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++14")

set(sources
	emp-tool/gc/backend.cpp
	emp-tool/io/net_io_channel.cpp 
	emp-tool/utils/prg.cpp)

LINK_DIRECTORIES(${cryptoTools_LINK_DIRS})

# Build both the static and dynamic library 
add_library(${NAME}        SHARED ${sources})
add_library(${NAME}_static STATIC ${sources})

# Name the static library the same
SET_TARGET_PROPERTIES(${NAME}_static PROPERTIES  OUTPUT_NAME ${NAME} CLEAN_DIRECT_OUTPUT 1)
#set_target_properties(${NAME} ${NAME}_static PROPERTIES DEBUG_POSTFIX d)





# install steps
macro (install_dir _dir _patterns)
	FOREACH(pattern ${_patterns})
		install(DIRECTORY ${CMAKE_SOURCE_DIR}/${_dir} DESTINATION include FILES_MATCHING PATTERN ${pattern})
	ENDFOREACH(pattern)
endmacro()

install(FILES emp-tool-config.cmake DESTINATION cmake/)
install(FILES "emp-tool.h" DESTINATION include)
install_dir(emp-tool "*.h;*.hpp;*/circuits/files/*.txt")
install(TARGETS ${NAME} DESTINATION lib)

set(LIBRARIES ${relic_LIBRARIES} ${cryptoTools_LIBRARIES} ${GMP_LIBRARIES})

target_link_libraries(${NAME}  ${LIBRARIES}) 



macro (add_test _name)
	add_executable("test_${_name}" "test/${_name}.cpp")
	target_link_libraries("test_${_name}"  ${NAME} ${LIBRARIES} ) 
endmacro()
add_test(bit)
add_test(gen_circuit)



