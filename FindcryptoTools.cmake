
set(cryptoTools_dirs
	${CMAKE_SOURCE_DIR}/../../libOTe/cryptoTools;
	${CMAKE_SOURCE_DIR}/../libOTe/cryptoTools)

	
if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Release" AND NOT  "${CMAKE_BUILD_TYPE}" STREQUAL "")
	set(__boost_debug_lib_modifier gd)
endif()
	
FOREACH(dir ${cryptoTools_dirs})
	if(EXISTS ${dir})
		set(cryptoTools_INCLUDE_DIRS 
			${dir};
			C:/libs/boost;
			C:/libs/miracl/miracl/include)
		
		if(MSVC)
			set(cryptoTools_LIBRARIES 
				"${dir}/../x64/\$(Configuration)/cryptoTools.lib;"
				"C:/libs/miracl/x64/\$(Configuration)/Miracl.lib")
			set(cryptoTools_LINK_DIRS C:/libs/boost/stage/lib)
		else()
			set(cryptoTools_LIBRARIES ${dir}/../lib/libcryptoTools.a;boost_system)
		endif()
		
		set(relic_FOUND true)
		break()
	endif()
endforeach(dir)

#message(STATUS "include    ${cryptoTools_INCLUDE_DIRS}")

if(NOT relic_FOUND)
	#message(STATUS "fallback")
	find_package(cryptoTools NO_MODULE)
endif()
