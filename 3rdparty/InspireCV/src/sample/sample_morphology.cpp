#include <iostream>
#include <inspirecv/inspirecv.h>
// #include <inspirecv/okcv/io/stb_warpper.h>

int main() {
    inspirecv::Image image = inspirecv::Image::Create();
    image.Read("../images/erode_before.jpg", 1);

    inspirecv::Image eroded = image.Erode(79, 1);
    eroded.Show("erode");

    inspirecv::Image image2 = inspirecv::Image::Create();
    image2.Read("../images/dilate_before.jpg", 1);

    inspirecv::Image dilated = image2.Dilate(2, 1);
    dilated.Show("dilate");

    dilated.Write("dilate.jpg");
    return 0;
}