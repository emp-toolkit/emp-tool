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

	
if(MSVC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /EHsc  -D_CRT_SECURE_NO_WARNINGS ")
	set(CMAKE_CXX_FLAGS_RELEASE "/MT /Z7")
	set(CMAKE_CXX_FLAGS_DEBUG "/MTd /Od /Z7")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF" CACHE STRING "" FORCE)
else()
	set(CMAKE_C_FLAGS "-pthread -Wall -march=native -O3 -maes")
endif()
set (CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++14")




set(sources 
	gc/backend.cpp 
	io/net_io_channel.cpp 
	utils/prg.cpp)

LINK_DIRECTORIES(${cryptoTools_LINK_DIRS})

# Build both the static and dynamic library 
add_library(${NAME}        SHARED ${sources})
add_library(${NAME}_static STATIC ${sources})

# Name the static library the same
SET_TARGET_PROPERTIES(${NAME}_static PROPERTIES  OUTPUT_NAME ${NAME} CLEAN_DIRECT_OUTPUT 1)
#set_target_properties(${NAME} ${NAME}_static PROPERTIES DEBUG_POSTFIX d)


#if(CMAKE_BUILD_TYPE)


# install steps
macro (install_dir _dir _patterns)
	FOREACH(pattern ${_patterns})
		install(DIRECTORY ${CMAKE_SOURCE_DIR}/${_dir} DESTINATION include/${NAME} FILES_MATCHING PATTERN ${pattern})
	ENDFOREACH(pattern)
endmacro()

install(FILES emp-tool-config.cmake DESTINATION cmake/)
install(FILES "emp-tool.h" DESTINATION include)
install_dir(io "*.h;*.hpp")
install_dir(garble "*.h;*.hpp")
install_dir(gc "*.h;*.hpp")
install_dir(circuits "*.h;*.hpp;files/*")
install_dir(utils "*.h;*.hpp")
install(TARGETS ${NAME} DESTINATION lib)


set(LIBRARIES ${relic_LIBRARIES} ${cryptoTools_LIBRARIES} ${GMP_LIBRARIES})

target_link_libraries(${NAME}  ${LIBRARIES}) 

message(STATUS  "sss  ${cryptoTools_LINK_DIRS}")

macro (add_test _name)
	add_executable("test_${_name}" "test/${_name}.cpp" ${basics} "gc/backend.cpp")
	target_link_libraries("test_${_name}"  ${NAME} ${LIBRARIES} ) 
endmacro()
add_test(bit)
add_test(gen_circuit)



