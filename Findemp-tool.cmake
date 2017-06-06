

if(MSVC AND EXISTS ${CMAKE_SOURCE_DIR}/../emp-tool)
	# on MSVC, try to use local source tree...
	set(EMP-TOOL_LIBRARIES "${CMAKE_SOURCE_DIR}/../emp-tool/bin/\$(Configuration)/emp-tool.lib")
	set(EMP-TOOL_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/../;${CMAKE_SOURCE_DIR}/../emp-tool)
	set(EMP-TOOL_FOUND true)
else()
	find_package(emp-tool NO_MODULES)
endif()