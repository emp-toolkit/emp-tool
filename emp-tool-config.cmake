find_package(OpenSSL)
find_package(relic)
find_path(EMP-TOOL_INCLUDE_DIR NAMES cmake/emp-tool-config.cmake)
find_library(EMP-TOOL_LIBRARY NAMES emp-tool)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(EMP-TOOL DEFAULT_MSG EMP-TOOL_INCLUDE_DIR EMP-TOOL_LIBRARY)

add_definitions(-DEMP_CIRCUIT_PATH=${EMP-TOOL_INCLUDE_DIR}/include/emp-tool/files/)
if(EMP-TOOL_FOUND)
	set(EMP-TOOL_LIBRARIES ${EMP-TOOL_LIBRARY} ${RELIC_LIBRARIES} ${OPENSSL_LIBRARIES} gmp)
	set(EMP-TOOL_INCLUDE_DIRS ${EMP-TOOL_INCLUDE_DIR}/include/emp-tool/ ${RELIC_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})
#	message("EMP-TOOL Found!")
endif()
