

if(MSVC AND EXISTS ${CMAKE_SOURCE_DIR}/../emp-ot)
	# on MSVC, try to use local source tree...
	set(EMP-OT_LIBRARIES )
	set(EMP-OT_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/../;${CMAKE_SOURCE_DIR}/../emp-ot)
	set(EMP-OT_FOUND true)
else()
	find_package(emp-ot NO_MODULES)
endif()