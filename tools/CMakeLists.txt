add_executable(empbc-tool empbc_tool.cpp)
target_link_libraries(empbc-tool PRIVATE emp-tool::emp-tool)
target_compile_features(empbc-tool PRIVATE cxx_std_20)
set_target_properties(empbc-tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(EMP_TOOL_INSTALL)
    install(TARGETS empbc-tool
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
