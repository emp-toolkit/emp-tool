
set(possible_gmp_h_dirs "C:/libs/mpir/lib/x64/")
set(possible_gmp_lib_dirs ${possible_gmp_h_dirs})
#
#FOREACH(dir ${gmp_dirs})
#	if(EXISTS ${dir})
#		set(gmp_INCLUDE_DIRS  "")
#		set(gmp_LIBRARIES "${dir}/lib/x64/\$(Configuration)/mpir.lib")
#		set(GMP_FOUND true)
#		break()
#	endif()
#endforeach(dir)
#
#
#if(NOT GMP_FOUND)
#	#message(STATUS "fallback")
#	find_package(gmp NO_MODULE)
#endif()
#
#
#if(NOT GMP_FOUND AND EXISTS ${CMAKE_INSTALL_PREFIX}/include/gmp.h)
#		set(gmp_INCLUDE_DIRS  "${CMAKE_INSTALL_PREFIX}/include/gmp.h")
#		set(gmp_LIBRARIES "${CMAKE_INSTALL_PREFIX}/lib/libgmp.a")
#		set(GMP_FOUND true)
#endif()
# Try to find the GMP librairies
# GMP_FOUND - system has GMP lib
# GMP_INCLUDE_DIRS - the GMP include directory
# GMP_LIBRARIES - Libraries needed to use GMP

if (GMP_INCLUDE_DIR AND GMP_LIBRARIES)
		# Already in cache, be silent
		set(GMP_FIND_QUIETLY TRUE)
endif (GMP_INCLUDE_DIR AND GMP_LIBRARIES)

if(MSVC)
	set(gmp_lib_name mpir)
else()
	set(gmp_lib_name gmp)
endif()

message(STATUS "s   ${possible_gmp_h_dirs}")
find_path(GMP_INCLUDE_DIRS NAMES gmp.h PATHS ${possible_gmp_h_dirs}/Release)
find_library(GMP_LIBRARIES NAMES ${gmp_lib_name} lib${gmp_lib_name} PATHS ${possible_gmp_lib_dirs}/Release)
find_library(GMPXX_LIBRARIES NAMES ${gmp_lib_name}xx lib${gmp_lib_name}xx PATHS ${possible_gmp_lib_dirs}/Release)
find_library(GMP_DEBUG_LIBRARIES NAMES ${gmp_lib_name} lib${gmp_lib_name} PATHS ${possible_gmp_lib_dirs}/Debug)
find_library(GMPXX_DEBUG_LIBRARIES NAMES ${gmp_lib_name}xx lib${gmp_lib_name}xx PATHS ${possible_gmp_lib_dirs}/Debug)
MESSAGE(STATUS "GMP libs: " ${GMP_LIBRARIES} " " ${GMPXX_LIBRARIES} )
MESSAGE(STATUS "GMP include: " ${GMP_INCLUDE_DIR})
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GMP DEFAULT_MSG GMP_INCLUDE_DIR GMP_LIBRARIES)

mark_as_advanced(GMP_INCLUDE_DIR GMP_LIBRARIES)