cmake_minimum_required(VERSION 3.5.1)

project(example)

set(CMAKE_CXX_STANDARD 14)

find_package(fineftp REQUIRED CONFIG)

set(sources
    src/main.cpp
)

add_executable (example
    ${sources}
)

target_link_libraries (example
    fineftp::server
)

# NOTE: This is added just to be able to find the automatically generated export file in the fineftp-server library. 
# This should not be needed when consuming fineftp-server from an installation folder via find_package.
target_include_directories(example
  PRIVATE
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/../fineftp-server/>          
)
