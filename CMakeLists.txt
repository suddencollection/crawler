cmake_minimum_required(VERSION 3.23)
project(crawler VERSION 0.1 LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# build library from src files
file(GLOB SRC_SOURCES "${CMAKE_SOURCE_DIR}/src/**.cpp")
add_library(crawler_lib STATIC ${SRC_SOURCES})

target_include_directories(crawler_lib PUBLIC
  ${CMAKE_SOURCE_DIR}/inc
)

# link curl and lexbor
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}")
find_package(CURL REQUIRED CONFIG)
find_package(Lexbor REQUIRED)
find_package(OGDF REQUIRED)
find_package(fmt REQUIRED)

target_link_libraries(crawler_lib
  PUBLIC
    CURL::libcurl
    lexbor::lexbor_static
    ogdf::ogdf
    fmt::fmt
)

# executable
add_executable(crawler_exe src/main.cpp)
target_link_libraries(crawler_exe PRIVATE crawler_lib)

# tests
add_subdirectory(tests)
