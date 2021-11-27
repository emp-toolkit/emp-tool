find_package(OpenSSL REQUIRED)

find_path(EMP-TOOL_INCLUDE_DIR NAMES emp-tool/emp-tool.h)
find_library(EMP-TOOL_LIBRARY NAMES emp-tool)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(emp-tool DEFAULT_MSG EMP-TOOL_INCLUDE_DIR EMP-TOOL_LIBRARY)

add_definitions(-DEMP_CIRCUIT_PATH=${EMP-TOOL_INCLUDE_DIR}/emp-tool/circuits/files/)
if(EMP-TOOL_FOUND)
	set(EMP-TOOL_LIBRARIES ${EMP-TOOL_LIBRARY} ${OPENSSL_LIBRARIES})# ${Boost_LIBRARIES})
	set(EMP-TOOL_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR} ${EMP-TOOL_INCLUDE_DIR})# ${Boost_INCLUDE_DIRS})
endif()
