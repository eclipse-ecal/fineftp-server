cmake_minimum_required(VERSION 3.5.1)

project(fineftp_example)

set(CMAKE_CXX_STANDARD 14)

set(CMAKE_FIND_PACKAGE_PREFER_CONFIG  TRUE)
find_package(fineftp REQUIRED)

set(sources
    src/main.cpp
)

add_executable (${PROJECT_NAME}
    ${sources}
)

target_link_libraries (${PROJECT_NAME}
    fineftp::server
)
