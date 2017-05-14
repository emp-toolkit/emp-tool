
set(gmp_dirs C:/libs/mpir)

FOREACH(dir ${gmp_dirs})
	if(EXISTS ${dir})
		set(gmp_INCLUDE_DIRS  "${dir}/lib/x64/\$(Configuration)")
		set(gmp_LIBRARIES "${dir}/lib/x64/\$(Configuration)/mpir.lib")
		set(gmp_FOUND true)
		break()
	endif()
endforeach(dir)

if(NOT gmp_INCLUDE_DIRS)
	#message(STATUS "fallback")
	find_package(gmp NO_MODULE)
endif()
