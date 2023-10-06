project(fineftp_test)

set(CMAKE_FIND_PACKAGE_PREFER_CONFIG  TRUE)

find_package(Threads REQUIRED)
find_package(GTest   REQUIRED)
find_package(fineftp REQUIRED)

set(FINEFTP_SERVER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../fineftp-server/src")

set(sources
  src/fineftp_test.cpp
)
set(fineftp_server_sources
    ${FINEFTP_SERVER_SRC_DIR}/filesystem.cpp
    ${FINEFTP_SERVER_SRC_DIR}/filesystem.h
    ${FINEFTP_SERVER_SRC_DIR}/win_str_convert.cpp
    ${FINEFTP_SERVER_SRC_DIR}/win_str_convert.h  
)

add_executable(${PROJECT_NAME} ${sources} ${fineftp_server_sources})

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    fineftp::server
    GTest::gtest_main)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${FINEFTP_SERVER_SRC_DIR}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES 
    ${sources}
)
source_group(TREE "${FINEFTP_SERVER_SRC_DIR}"
            PREFIX "fineftp-server"
            FILES 
                ${fineftp_server_sources}
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})