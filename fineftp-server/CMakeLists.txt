cmake_minimum_required(VERSION 3.5.1)

project(server)
set(PROJECT_VERSION_STRING 0.1.0)
set(PROJECT_VERSION_MAJOR 0)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
find_package(asio REQUIRED)

# Public API include directory
set (includes
    include/fineftp/server.h
    include/fineftp/permissions.h
)

# Private source files
set(sources
    src/filesystem.cpp
    src/filesystem.h
    src/ftp_message.h
    src/ftp_session.cpp
    src/ftp_session.h
    src/ftp_user.h
    src/server.cpp
    src/server_impl.cpp
    src/server_impl.h
    src/user_database.cpp
    src/user_database.h
)

add_library (${PROJECT_NAME}
    ${includes}
    ${sources}
)
add_library (fineftp::${PROJECT_NAME} ALIAS ${PROJECT_NAME})


target_link_libraries(${PROJECT_NAME}
    PRIVATE
        asio::asio
        Threads::Threads
)

target_compile_definitions(${PROJECT_NAME} PRIVATE ASIO_STANDALONE)
target_compile_definitions(${PROJECT_NAME} PRIVATE __USE_FILE_OFFSET64=1)
target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0601)


target_compile_options(${PROJECT_NAME} PRIVATE
                           $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
                                -Wall -Wextra>
                           $<$<CXX_COMPILER_ID:MSVC>:
                                /W4>)


# Add own public include directory
target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                                  $<INSTALL_INTERFACE:include>)
target_include_directories(${PROJECT_NAME} PRIVATE src/)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION_STRING}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME fineftp-${PROJECT_NAME}
)

##################################

if (MSVC)
    # Mirror the folder structure with Visual Studio filters
    include(../msvc_sourcetree.cmake)
    msvc_source_tree(${includes} ${sources})
endif()

##################################

set(FINEFTP_INSTALL_CMAKE_DIR "lib/cmake/fineftp")

install(
  TARGETS ${PROJECT_NAME}
  EXPORT fineftpTargets 
  LIBRARY DESTINATION "lib" 
  ARCHIVE DESTINATION "lib"   
)

install(DIRECTORY "include/fineftp" DESTINATION "include" FILES_MATCHING PATTERN "*.h")
install(
  EXPORT fineftpTargets 
  FILE fineftpTargets.cmake 
  DESTINATION ${FINEFTP_INSTALL_CMAKE_DIR}
  NAMESPACE fineftp::
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
  "../cmake/fineftpConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/fineftpConfig.cmake"
  INSTALL_DESTINATION ${FINEFTP_INSTALL_CMAKE_DIR}
  PATH_VARS FINEFTP_INSTALL_CMAKE_DIR
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cmake/fineftpConfig.cmake" DESTINATION ${FINEFTP_INSTALL_CMAKE_DIR})

