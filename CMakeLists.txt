cmake_minimum_required (VERSION 2.8.11)
project (emptool)
set(NAME "emp-tool")

include_directories(${CMAKE_SOURCE_DIR}/..)

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )

set (CMAKE_C_FLAGS " -Wall -pthread -march=native -O3 -maes")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg -ggdb")
set (CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++14")

# OS specific flags
if(MSVC)
	# =================== WINDOWS =============================
	if(NOT  EXISTS ${libOTe_dir})
		set(libOTe_dir "${CMAKE_SOURCE_DIR}/../../libOTe/")
	endif()

	# Set up LD_LIBRARY_PATH and includes path for windows
	set( relic_dir "${CMAKE_SOURCE_DIR}/../../relic")
	set( mpir_dir "C:/libs/mpir")
	set( miracl_dir "C:/libs/miracl/")
	set( boost_dir "C:/libs/boost")

	include_directories("${libOTe_dir}/cryptoTools")
	include_directories("${relic_dir}/include")
	include_directories("${mpir_dir}/lib/x64/\$(Configuration)")
	include_directories("${boost_dir}")
	include_directories("${miracl_dir}/miracl/include")

	link_directories("${CMAKE_SOURCE_DIR}")
	link_directories("${libOTe_dir}/x64/\$(Configuration)")
	link_directories("${relic_dir}/lib/\$(Configuration)")
	link_directories("${mpir_dir}/lib/x64/\$(Configuration)")
	link_directories("${boost_dir}/stage/lib")
	link_directories("${miracl_dir}/x64/\$(Configuration)")


	set(gmp "mpir")
	set(relic "relic_s")
	set(boost "")
else(MSVC)
	# =================== LINUX =============================
   	set(gmp "gmp")
	set(relic "relic")
	set(boost "boost_system boost_thread")
endif(MSVC)


MESSAGE( STATUS "CMAKE_CXX_COMPILER: " ${CMAKE_CXX_COMPILER} )

file(GLOB headers 
	"io/*.h"
	"circuits/*.h"
	"circuits/*.hpp"
	"utils/*.h"
	"utils/*.hpp"
	"gc/*.h"
	"gc/*.hpp"
#	"emp-tool"
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

# Build both the static and dynamic library 
add_library(${NAME}        SHARED ${sources})
add_library(${NAME}_static STATIC ${sources})

# Name the static library the same
SET_TARGET_PROPERTIES(${NAME}_static PROPERTIES  OUTPUT_NAME ${NAME} CLEAN_DIRECT_OUTPUT 1)

# install steps
install(FILES emp-tool-config.cmake DESTINATION cmake/)



message(STATUS ${headers})
install(FILES "emp-tool" DESTINATION include)
install(FILES ${headers} DESTINATION include/${NAME})
install(FILES ${libgarble} DESTINATION include/${NAME}/garble)
install(FILES ${circuit-files} DESTINATION include/${NAME}/files)
install(TARGETS ${NAME} DESTINATION lib)


macro (add_test _name)
	add_executable("test_${_name}" "test/${_name}.cpp" ${basics} "gc/backend.cpp")
	target_link_libraries("test_${_name}"  ${NAME}  ${relic} cryptoTools miracl ${gmp} ${boost}) 
endmacro()
add_test(bit)
add_test(gen_circuit)



if(MSVC)
	# Force static std runtime library on windows
	FOREACH(flag
	 CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
	 CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG_INIT
	 CMAKE_CXX_FLAGS_RELEASE  CMAKE_CXX_FLAGS_RELWITHDEBINFO
	 CMAKE_CXX_FLAGS_DEBUG  CMAKE_CXX_FLAGS_DEBUG_INIT)
	 STRING(REPLACE "/MD"  "/MT" "${flag}" "${${flag}}")
	 SET("${flag}" "${${flag}} /EHsc")
	ENDFOREACH()
endif()
