#pragma once
#ifndef BIGGUYSMAIN_TEST_SETTINGS_H
#define BIGGUYSMAIN_TEST_SETTINGS_H
#include <catch2/catch.hpp>
#include <iostream>
#include "test_helper.h"

struct test_case_split {
    ~test_case_split() {
        std::cout
          << "==============================================================================="
          << std::endl;
    }

#define DRAW_SPLIT_LINE test_case_split split_line_x;
};

#endif  // BIGGUYSMAIN_TEST_SETTINGS_H