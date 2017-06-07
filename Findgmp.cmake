# Try to find the GMP librairies
# gmp_FOUND - system has GMP lib
# gmp_INCLUDE_DIRS - the GMP include directory
# GMP_LIBRARIES - Libraries needed to use GMP

#if (GMP_INCLUDE_DIRS AND GMP_LIBRARIES)
		# Already in cache, be silent
#		set(GMP_FIND_QUIETLY TRUE)
#endif (GMP_INCLUDE_DIRS AND GMP_LIBRARIES)



find_path   (GMP_INCLUDE_DIRS    NAMES gmp.h     )
find_library(GMP_LIBRARIES       NAMES gmp libgmp)
	
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GMP DEFAULT_MSG GMP_INCLUDE_DIR GMP_LIBRARIES)

