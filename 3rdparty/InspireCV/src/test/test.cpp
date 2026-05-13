#include <string>
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <iostream>
#include "inspirecv/inspirecv.h"

int main(int argc, char *argv[]) {
    Catch::Session session;
    session.applyCommandLine(argc, argv);

    auto result = session.run();

    std::cout << inspirecv::GetVersion() << std::endl;
    return result;
}