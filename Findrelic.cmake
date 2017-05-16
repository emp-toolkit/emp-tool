
set(relic_dirs
	${CMAKE_SOURCE_DIR}/../../relic;
	${CMAKE_SOURCE_DIR}/../relic)

FOREACH(dir ${relic_dirs})
	if(EXISTS ${dir})
		set(relic_INCLUDE_DIRS  ${dir}/include;)
		if(MSVC)
			set(relic_LIBRARIES "${dir}/lib/\$(Configuration)/relic_s.lib")
		else()
			set(relic_LIBRARIES ${dir}/lib/librelic_s.a)
		endif()
		set(relic_FOUND true)
		break()
	endif()
endforeach(dir)

if(NOT relic_INCLUDE_DIRS)
	#message(STATUS "fallback")
	find_package(relic NO_MODULE)
endif()
