#include <iostream>
#include <inspirecv/inspirecv.h>
// #include <inspirecv/okcv/io/stb_warpper.h>

int main() {
    inspirecv::Image image = inspirecv::Image::Create();
    image.Read("../images/r0.jpg");
    image.Show();

    auto resized = image.Resize(640, 480);
    auto gray = resized.ToGray();
    gray.Show();

    std::cout << gray << std::endl;

    inspirecv::Image ud = inspirecv::Image::Create();

    ud.Read("../images/r0.jpg");
    ud.Write("stb_r0_write.jpg");

    // return 0;
}