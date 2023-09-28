project(fineftp_test)

set(CMAKE_FIND_PACKAGE_PREFER_CONFIG  TRUE)

find_package(Threads REQUIRED)
find_package(GTest   REQUIRED)
find_package(fineftp REQUIRED)

set(sources
  src/fineftp_test.cpp
)

add_executable(${PROJECT_NAME} ${sources})

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    fineftp::server
    GTest::gtest_main)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_14)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES 
    ${sources}
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})