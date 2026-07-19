# Downstream-consumer smoke test: build a tiny program against an INSTALLED
# emp-tool via find_package, exactly as an external project would. Driven by
# test/install_consumer/run.sh and the release-package CI job; not part of the
# emp-tool build itself.
cmake_minimum_required(VERSION 3.25)
project(emp_tool_install_consumer LANGUAGES CXX)

find_package(emp-tool 1.0 REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE emp-tool::emp-tool)
