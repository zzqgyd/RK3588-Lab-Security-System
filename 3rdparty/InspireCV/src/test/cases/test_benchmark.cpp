#include "../common/common.h"
#include <inspirecv/inspirecv.h>
#include <inspirecv/time_spend.h>

TEST_CASE("test_benchmark", "[benchmark]") {
    DRAW_SPLIT_LINE;
    auto t_resize = inspirecv::TimeSpend("Resize");
    auto t_affine = inspirecv::TimeSpend("Affine");
    SECTION("resize") {
        auto img = inspirecv::Image::Create(1024, 1024, 3, nullptr);
        int loop = 100;
        for (int i = 0; i < loop; ++i) {
            t_resize.Start();
            auto resized_img = img.Resize(512, 512);
            t_resize.Stop();
        }
        std::cout << t_resize << std::endl;
    }

    SECTION("affine") {
        auto img = inspirecv::Image::Create(1024, 1024, 3, nullptr);
        auto rot90 = inspirecv::TransformMatrix::Rotate90();
        int loop = 100;
        for (int i = 0; i < loop; ++i) {
            t_affine.Start();
            auto affine_img = img.WarpAffine(rot90, 1024, 1024);
            t_affine.Stop();
        }
        std::cout << t_affine << std::endl;
    }
}