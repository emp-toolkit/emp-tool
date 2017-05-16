

if(MSVC AND EXISTS ${CMAKE_SOURCE_DIR}/../emp-tool)
	# on MSVC, try to use local source tree...
	set(EMP-TOOL_LIBRARIES "${CMAKE_SOURCE_DIR}/../emp-tool/bin/\$(Configuration)/emp-tool.lib")
	set(EMP-TOOL_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/../;${CMAKE_SOURCE_DIR}/../emp-tool)
	set(EMP-TOOL_FOUND true)
else()
	# find the location of where this file was installed. We expect includes/libs to be next to it
	find_path(EMP-TOOL_INSTALL_DIR NAMES cmake/emp-tool-config.cmake)


	find_library(EMP-TOOL_LIBRARY NAMES emp-tool)

	include(FindPackageHandleStandardArgs)
	find_package_handle_standard_args(EMP-TOOL       DEFAULT_MSG EMP-TOOL_INSTALL_DIR EMP-TOOL_LIBRARY)
	#find_package_handle_standard_args(EMP-TOOL_DEBUG DEFAULT_MSG EMP-TOOL_INSTALL_DIR EMP-TOOL_DEBUG_LIBRARY)

	add_definitions(-DEMP_CIRCUIT_PATH=${EMP-TOOL_INSTALL_DIR}/include/emp-tool/files/)
	if(EMP-TOOL_FOUND)
		set(EMP-TOOL_LIBRARIES ${EMP-TOOL_LIBRARY})
		set(EMP-TOOL_INCLUDE_DIRS ${EMP-TOOL_INSTALL_DIR}/include/)
	endif()

endif()