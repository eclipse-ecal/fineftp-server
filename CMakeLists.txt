cmake_minimum_required(VERSION 3.5.1)

include(CMakeDependentOption)

# Project call
include("${CMAKE_CURRENT_LIST_DIR}/fineftp-server/version.cmake")
project(fineftp VERSION ${FINEFTP_SERVER_VERSION_MAJOR}.${FINEFTP_SERVER_VERSION_MINOR}.${FINEFTP_SERVER_VERSION_PATCH})

# Normalize backslashes from Windows paths
file(TO_CMAKE_PATH "${CMAKE_MODULE_PATH}" CMAKE_MODULE_PATH)
file(TO_CMAKE_PATH "${CMAKE_PREFIX_PATH}" CMAKE_PREFIX_PATH)
message(STATUS "Module Path: ${CMAKE_MODULE_PATH}")
message(STATUS "Prefix Path: ${CMAKE_PREFIX_PATH}")

# CMake Options
option(FINEFTP_SERVER_BUILD_SAMPLES
       "Build project samples."
       ON)
option(FINEFTP_SERVER_BUILD_TESTS
       "Build the the fineftp-server tests. Requires C++17. For executing the tests, curl must be available from the PATH."
       OFF)

option(FINEFTP_SERVER_USE_BUILTIN_ASIO
        "Use the builtin asio submodule. If set to OFF, asio must be available from somewhere else (e.g. system libs)."
        ON)
cmake_dependent_option(FINEFTP_SERVER_USE_BUILTIN_GTEST
        "Use the builtin GoogleTest submodule. Only needed if FINEFTP_SERVER_BUILD_TESTS is ON. If set to OFF, GoogleTest must be available from somewhere else (e.g. system libs)."
        ON                              # Default value if dependency is met
        "FINEFTP_SERVER_BUILD_TESTS"    # Dependency
        OFF)                            # Default value if dependency is not met

# Set Debug postfix
set(CMAKE_DEBUG_POSTFIX            d)
set(CMAKE_MINSIZEREL_POSTFIX       minsize)
set(CMAKE_RELWITHDEBINFO_POSTFIX   reldbg)

# Use builtin asio
if (FINEFTP_SERVER_USE_BUILTIN_ASIO)
    include("${CMAKE_CURRENT_LIST_DIR}/thirdparty/build-asio.cmake")
endif()

# Use builtin gtest
if (FINEFTP_SERVER_USE_BUILTIN_GTEST)
    include("${CMAKE_CURRENT_LIST_DIR}/thirdparty/build-gtest.cmake")
endif()

# For tests we need to make sure that all shared libraries and executables are 
# put into the same directory. Otherwise the tests will fail on windows.
if(FINEFTP_SERVER_BUILD_TESTS AND BUILD_SHARED_LIBS AND FINEFTP_SERVER_USE_BUILTIN_GTEST)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# Add main fineftp::server library
add_subdirectory(fineftp-server)

# Add the fineftp::server dummy module
# Module path for finding asio
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/fineftp-module)

if (FINEFTP_SERVER_BUILD_SAMPLES)
    add_subdirectory(samples/fineftp_example)
endif()

if (FINEFTP_SERVER_BUILD_TESTS)
    enable_testing()
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/tests/fineftp_test")
endif()

# Make this package available for packing with CPack
include("${CMAKE_CURRENT_LIST_DIR}/cpack_config.cmake")