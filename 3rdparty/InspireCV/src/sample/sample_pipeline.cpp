#include <iostream>
#include <vector>
#include "inspirecv/inspirecv.h"

using namespace inspirecv;

int main() {
    // Inputs
    const std::string target_path = "../images/face_sample.png";
    const std::string fake_path = "../images/bgr_fake.jpg";

    // Read images
    Image target_img = Image::Create(target_path, 3);
    if (target_img.Empty()) {
        std::cerr << "Failed to read target image: " << target_path << std::endl;
        return 1;
    }
    Image bgr_fake = Image::Create(fake_path, 3);
    if (bgr_fake.Empty()) {
        std::cerr << "Failed to read bgr_fake: " << fake_path << std::endl;
        return 1;
    }

    inspirecv::TimeSpend t1("WarpAffine-"+std::string(GetCVBackend()));
    t1.Start();
    // Default affine M (2x3)
    // [[  0.18727215   0.06365608 -34.09374106]
    //  [ -0.06365608   0.18727215  -5.86148856]]
    TransformMatrix M = TransformMatrix::Create(
        0.18727215f, 0.06365608f, -34.09374106f,
       -0.06365608f, 0.18727215f,  -5.86148856f
    );

    // Read aligned image (aimg) from file
    const std::string aimg_path = "../images/aimg.jpg";
    Image aimg = Image::Create(aimg_path, 3);
    if (aimg.Empty()) {
        std::cerr << "Failed to read aimg: " << aimg_path << std::endl;
        return 1;
    }
    const int aligned_w = aimg.Width();
    const int aligned_h = aimg.Height();

    // removed fake_diff pipeline (absdiff->warp->threshold) to reduce compute

    // IM = inverse(M)
    TransformMatrix IM = M.GetInverse();

    // img_white (aligned space, 1-channel filled with 255)
    Image img_white = Image::Create(aligned_w, aligned_h, 1);
    img_white.Fill(255);
    

    // Warp to target space using IM
    const int TW = target_img.Width();
    const int TH = target_img.Height();
    Image bgr_fake_warp = bgr_fake.WarpAffine(M, TW, TH);
    Image img_white_warp = img_white.WarpAffine(M, TW, TH);
    // removed: fake_diff_warp

    // bgr_fake_warp.Show("bgr_fake_warp");
    // img_white_warp.Show("img_white_warp");
    // fake_diff_warp.Show("fake_diff_warp");

    // Thresholds
    Image img_white_bin = img_white_warp.Threshold(20, 255, 0);
    // removed: fake_diff_bin

    // Estimate mask size k based on image size (approximate np.where bbox)
    int mask_size = static_cast<int>(std::sqrt(static_cast<double>(TW) * TH));
    int k1 = std::max(mask_size / 10, 10);
    int k2 = std::max(mask_size / 20, 5);

    // Morphology and blurs
    TimeSpend t2("Erode-"+std::string(GetCVBackend()));
    t2.Start();
    Image img_mask = img_white_bin.Erode(k1, 1);
    t2.Stop();
    std::cout << t2 << std::endl;
    // removed: fake_diff_bin dilate

    // Gaussian blurs (ensure odd kernel)
    int blur1 = 2 * k2 + 1;
    img_mask = img_mask.GaussianBlur(blur1, 0.0);
    
    // removed: fake_diff_bin blur

    // bgr_fake_warp.Show("bgr_fake_warp");
    // target_img.Show("target_img");
    // img_mask.Show("img_mask");
    
    // Blend: out = img_mask/255 * bgr_fake_warp + (1 - img_mask/255) * target_img
    Image fake_merged = bgr_fake_warp.Blend(target_img, img_mask);

    t1.Stop();
    std::cout << t1 << std::endl;

    // Save result
    const std::string output_path = "pipeline_out_"+std::string(GetCVBackend())+".jpg";
    fake_merged.Write(output_path);

    std::cout << "Pipeline finished. Output: " << output_path << std::endl;
    return 0;
}


