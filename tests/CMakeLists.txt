# tests/CMakeLists.txt

find_package(doctest CONFIG REQUIRED)
file(GLOB TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)


add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests
  PRIVATE
    crawler_lib      # link production code
    doctest::doctest # link the test framework
)

target_include_directories(tests PRIVATE
  ${PROJECT_SOURCE_DIR}/inc
  ${CMAKE_CURRENT_SOURCE_DIR}/inc # test-specific headers
)

add_test(NAME all_tests COMMAND tests --reporters=console)
