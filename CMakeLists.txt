cmake_minimum_required(VERSION 3.16)

project(crawler LANGUAGES CXX)

# Use C++17 or later
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(inc)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS src/*.cpp)

# Add executable
add_executable(crawler ${SRC_FILES})
