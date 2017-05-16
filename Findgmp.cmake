# Try to find the GMP librairies
# gmp_FOUND - system has GMP lib
# gmp_INCLUDE_DIRS - the GMP include directory
# GMP_LIBRARIES - Libraries needed to use GMP

#if (GMP_INCLUDE_DIRS AND GMP_LIBRARIES)
		# Already in cache, be silent
#		set(GMP_FIND_QUIETLY TRUE)
#endif (GMP_INCLUDE_DIRS AND GMP_LIBRARIES)



if(MSVC)

	set(possible_gmp_h_dirs "C:/libs/mpir/")
	set(possible_gmp_lib_dirs ${possible_gmp_h_dirs})
	
	FOREACH(dir ${possible_gmp_h_dirs})
		if(EXISTS ${dir})
			set(GMP_INCLUDE_DIRS  "${dir}/lib/x64/\$(Configuration)")
			set(GMP_LIBRARIES "${dir}/lib/x64/\$(Configuration)/mpir.lib")
			set(GMP_FOUND true)
			break()
		endif()
	endforeach(dir)
else()
	

	find_path   (GMP_INCLUDE_DIRS    NAMES gmp.h     )
	find_library(GMP_LIBRARIES       NAMES gmp libgmp)
	
	include(FindPackageHandleStandardArgs)
	FIND_PACKAGE_HANDLE_STANDARD_ARGS(GMP DEFAULT_MSG GMP_INCLUDE_DIR GMP_LIBRARIES)
endif()
