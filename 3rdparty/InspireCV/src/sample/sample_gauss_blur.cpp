#include <iostream>
#include <inspirecv/inspirecv.h>
// #include <inspirecv/okcv/io/stb_warpper.h>

int main() {
    inspirecv::Image image = inspirecv::Image::Create();
    image.Read("../images/fake_diff_after.jpg");

    inspirecv::Image blurred;
    inspirecv::TimeSpend ts(inspirecv::GetCVBackend());
    for (int i = 0; i < 100; ++i) {  
        ts.Start();
        blurred = image.GaussianBlur(11, 0.0);
        ts.Stop();
    }
    std::cout << ts << std::endl;
    blurred.Write("gaussian_blur.jpg");

    return 0;
}