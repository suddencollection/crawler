#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "main.hpp"  // The header you're testing
#include <doctest/doctest.h>
#include "main_test.hpp"


TEST_CASE("main.hpp sanity check") {
    CHECK(2+ 3 == 5);
    CHECK(-1+ 1 == 0);
    CHECK(a == 99);
}

