#include "../common/common.h"
#include <inspirecv/inspirecv.h>

TEST_CASE("test_image_basic_operations", "[basic]") {
    SECTION("Test image creation and data access") {
        // 2 * 3 * 3
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        auto image = inspirecv::Image::Create(2, 3, 3, data_uint8);

        REQUIRE_EQ_C_ARRAY(image.Data(), data_uint8, 2 * 3 * 3);

        // 2 * 3
        uint8_t data_gray_uint8[] = {1, 2, 3, 4, 5, 6};
        auto image_gray = inspirecv::Image::Create(2, 3, 1, data_gray_uint8);
        REQUIRE_EQ_C_ARRAY(image_gray.Data(), data_gray_uint8, 2 * 3);
    }

    SECTION("Test resize no-op") {
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        auto image = inspirecv::Image::Create(2, 3, 3, data_uint8);
        auto resized_img = image.Resize(2, 3);
        REQUIRE_EQ_C_ARRAY(resized_img.Data(), data_uint8, 2 * 3 * 3);

        // 2 * 3
        uint8_t data_gray_uint8[] = {1, 2, 3, 4, 5, 6};
        auto image_gray = inspirecv::Image::Create(2, 3, 1, data_gray_uint8);
        auto resized_img_gray = image_gray.Resize(2, 3);
        REQUIRE_EQ_C_ARRAY(resized_img_gray.Data(), data_gray_uint8, 2 * 3);
    }

    SECTION("Test warp affine no-op") {
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        auto image = inspirecv::Image::Create(2, 3, 3, data_uint8);
        auto transform = inspirecv::TransformMatrix::Create(1, 0, 0, 0, 1, 0);
        auto warped_img = image.WarpAffine(transform, 2, 3);
        REQUIRE_EQ_C_ARRAY(warped_img.Data(), data_uint8, 2 * 3 * 3);

        uint8_t data_gray_uint8[] = {1, 2, 3, 4, 5, 6};
        auto image_gray = inspirecv::Image::Create(2, 3, 1, data_gray_uint8);
        auto warped_img_gray = image_gray.WarpAffine(transform, 2, 3);
        REQUIRE_EQ_C_ARRAY(warped_img_gray.Data(), data_gray_uint8, 2 * 3);
    }

    SECTION("Test add no-op") {
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        auto image = inspirecv::Image::Create(2, 3, 3, data_uint8);
        auto added_img = image.Add(0);
        REQUIRE_EQ_C_ARRAY(added_img.Data(), data_uint8, 2 * 3 * 3);

        uint8_t data_gray_uint8[] = {1, 2, 3, 4, 5, 6};
        auto image_gray = inspirecv::Image::Create(2, 3, 1, data_gray_uint8);
        auto added_img_gray = image_gray.Add(0);
        REQUIRE_EQ_C_ARRAY(added_img_gray.Data(), data_gray_uint8, 2 * 3);
    }

    SECTION("Test multiply no-op") {
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        auto image = inspirecv::Image::Create(2, 3, 3, data_uint8);
        auto mul_img = image.Mul(1);
        REQUIRE_EQ_C_ARRAY(mul_img.Data(), data_uint8, 2 * 3 * 3);

        uint8_t data_gray_uint8[] = {1, 2, 3, 4, 5, 6};
        auto image_gray = inspirecv::Image::Create(2, 3, 1, data_gray_uint8);
        auto mul_img_gray = image_gray.Mul(1);
        REQUIRE_EQ_C_ARRAY(mul_img_gray.Data(), data_gray_uint8, 2 * 3);
    }

    SECTION("Test crop no-op") {
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        auto image = inspirecv::Image::Create(2, 3, 3, data_uint8);
        auto cropped_img = image.Crop(inspirecv::Rect<int>(0, 0, 2, 3));
        REQUIRE_EQ_C_ARRAY(cropped_img.Data(), data_uint8, 2 * 3 * 3);

        uint8_t data_gray_uint8[] = {1, 2, 3, 4, 5, 6};
        auto image_gray = inspirecv::Image::Create(2, 3, 1, data_gray_uint8);
        auto cropped_img_gray = image_gray.Crop(inspirecv::Rect<int>(0, 0, 2, 3));
        REQUIRE_EQ_C_ARRAY(cropped_img_gray.Data(), data_gray_uint8, 2 * 3);
    }

    SECTION("Test rotation no-op") {
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        auto image = inspirecv::Image::Create(2, 3, 3, data_uint8);
        auto rotated_img = image.Rotate90().Rotate270();
        REQUIRE_EQ_C_ARRAY(rotated_img.Data(), data_uint8, 2 * 3 * 3);

        uint8_t data_gray_uint8[] = {1, 2, 3, 4, 5, 6};
        auto image_gray = inspirecv::Image::Create(2, 3, 1, data_gray_uint8);
        auto rotated_img_gray = image_gray.Rotate90().Rotate270();
        REQUIRE_EQ_C_ARRAY(rotated_img_gray.Data(), data_gray_uint8, 2 * 3);
    }

    SECTION("Test horizontal flip no-op") {
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        auto image = inspirecv::Image::Create(2, 3, 3, data_uint8);
        auto flipped_horizontally_img = image.FlipHorizontal().FlipHorizontal();
        REQUIRE_EQ_C_ARRAY(flipped_horizontally_img.Data(), data_uint8, 2 * 3 * 3);

        uint8_t data_gray_uint8[] = {1, 2, 3, 4, 5, 6};
        auto image_gray = inspirecv::Image::Create(2, 3, 1, data_gray_uint8);
        auto flipped_horizontally_img_gray = image_gray.FlipHorizontal().FlipHorizontal();
        REQUIRE_EQ_C_ARRAY(flipped_horizontally_img_gray.Data(), data_gray_uint8, 2 * 3);
    }

    SECTION("Test vertical flip no-op") {
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        auto image = inspirecv::Image::Create(2, 3, 3, data_uint8);
        auto flipped_vertically_img = image.FlipVertical().FlipVertical();
        REQUIRE_EQ_C_ARRAY(flipped_vertically_img.Data(), data_uint8, 2 * 3 * 3);

        uint8_t data_gray_uint8[] = {1, 2, 3, 4, 5, 6};
        auto image_gray = inspirecv::Image::Create(2, 3, 1, data_gray_uint8);
        auto flipped_vertically_img_gray = image_gray.FlipVertical().FlipVertical();
        REQUIRE_EQ_C_ARRAY(flipped_vertically_img_gray.Data(), data_gray_uint8, 2 * 3);
    }

    SECTION("Test image content changes") {
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        uint8_t data_gray_uint8[] = {1, 2, 3, 4, 5, 6};
        int width = 2;
        int height = 3;
        int channels = 3;
        auto image = inspirecv::Image::Create(width, height, channels, data_uint8);
        auto image_gray = inspirecv::Image::Create(width, height, 1, data_gray_uint8);
        SECTION("Test image resize") {
            auto resized_img = image.Resize(1, 2);
            REQUIRE(resized_img.Width() == 1);
            REQUIRE(resized_img.Height() == 2);

            auto resized_img_gray = image_gray.Resize(1, 2);
            REQUIRE(resized_img_gray.Width() == 1);
            REQUIRE(resized_img_gray.Height() == 2);
        }

        SECTION("Test image rotate api") {
            uint8_t data_uint8_rot90[] = {13, 14, 15, 7,  8,  9,  1, 2, 3,
                                          16, 17, 18, 10, 11, 12, 4, 5, 6};
            auto rotated_img = image.Rotate90();
            REQUIRE(rotated_img.Width() == image.Height());
            REQUIRE(rotated_img.Height() == image.Width());
            REQUIRE_EQ_C_ARRAY(rotated_img.Data(), data_uint8_rot90, 3 * 2 * 3);

            uint8_t data_uint8_rot270[] = {4, 5, 6, 10, 11, 12, 16, 17, 18,
                                           1, 2, 3, 7,  8,  9,  13, 14, 15};
            auto rotated_img2 = image.Rotate270();
            REQUIRE(rotated_img2.Width() == image.Height());
            REQUIRE(rotated_img2.Height() == image.Width());
            REQUIRE_EQ_C_ARRAY(rotated_img2.Data(), data_uint8_rot270, 3 * 2 * 3);

            // gray
            uint8_t data_gray_uint8_rot90[] = {5, 3, 1, 6, 4, 2};
            auto rotated_img_gray = image_gray.Rotate90();
            REQUIRE(rotated_img_gray.Width() == image_gray.Height());
            REQUIRE(rotated_img_gray.Height() == image_gray.Width());
            REQUIRE_EQ_C_ARRAY(rotated_img_gray.Data(), data_gray_uint8_rot90, 3 * 2 * 1);
        }

        SECTION("Test image warp affine api") {
            uint8_t data_uint8_rot180[] = {16, 17, 18, 13, 14, 15, 10, 11, 12,
                                           7,  8,  9,  4,  5,  6,  1,  2,  3};
            auto transform_rot180 =
              inspirecv::TransformMatrix::Create(-1, 0, width - 1, 0, -1, height - 1);
            auto warped_img = image.WarpAffine(transform_rot180, image.Width(), image.Height());
            REQUIRE(warped_img.Width() == image.Width());
            REQUIRE(warped_img.Height() == image.Height());
            REQUIRE_EQ_C_ARRAY(warped_img.Data(), data_uint8_rot180, 3 * 2 * 3);

            auto image_rot180 = image.Rotate180();
            REQUIRE(image_rot180.Width() == image.Width());
            REQUIRE(image_rot180.Height() == image.Height());
            REQUIRE_EQ_C_ARRAY(image_rot180.Data(), data_uint8_rot180, 3 * 2 * 3);

            // gray
            uint8_t data_gray_uint8_rot180[] = {6, 5, 4, 3, 2, 1};
            auto warped_img_gray =
              image_gray.WarpAffine(transform_rot180, image_gray.Width(), image_gray.Height());
            REQUIRE_EQ_C_ARRAY(warped_img_gray.Data(), data_gray_uint8_rot180, 3 * 2 * 1);

            auto image_rot180_gray = image_gray.Rotate180();
            REQUIRE(image_rot180_gray.Width() == image_gray.Width());
            REQUIRE(image_rot180_gray.Height() == image_gray.Height());
            REQUIRE_EQ_C_ARRAY(image_rot180_gray.Data(), data_gray_uint8_rot180, 3 * 2 * 1);
        }

        SECTION("Test image crop") {
            uint8_t expected_crop[] = {1, 2, 3, 7, 8, 9};
            auto cropped_img = image.Crop(inspirecv::Rect<int>(0, 0, 1, 2));
            REQUIRE(cropped_img.Width() == 1);
            REQUIRE(cropped_img.Height() == 2);
            REQUIRE_EQ_C_ARRAY(cropped_img.Data(), expected_crop, 2 * 1 * 3);

            // gray
            uint8_t expected_crop_gray[] = {1, 3};
            auto cropped_img_gray = image_gray.Crop(inspirecv::Rect<int>(0, 0, 1, 2));
            REQUIRE(cropped_img_gray.Width() == 1);
            REQUIRE(cropped_img_gray.Height() == 2);
            REQUIRE_EQ_C_ARRAY(cropped_img_gray.Data(), expected_crop_gray, 2 * 1 * 1);
        }

        SECTION("Test image pad") {
            // clang-format off
            uint8_t expected_pad[] = {  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,
                                        0, 0, 0, 1,  2,  3,  4,  5,  6,  0,  0,  0,
                                        0, 0, 0, 7,  8,  9,  10, 11, 12, 0,  0,  0,
                                        0, 0, 0, 13, 14, 15, 16, 17, 18, 0,  0,  0,
                                        0, 0, 0,  0,  0,  0,  0,  0,  0, 0,  0,  0  };
            // clang-format on
            auto padded_img = image.Pad(1, 1, 1, 1, {0, 0, 0});
            REQUIRE(padded_img.Width() == image.Width() + 2);
            REQUIRE(padded_img.Height() == image.Height() + 2);
            REQUIRE_EQ_C_ARRAY(padded_img.Data(), expected_pad, 3 * 4 * 3);

            // gray

            // clang-format off
            uint8_t expected_pad_gray[] = { 0, 0, 0, 0,   
                                            0, 1, 2, 0,   
                                            0, 3, 4, 0,   
                                            0, 5, 6, 0,  
                                            0, 0, 0, 0};
            // clang-format on  
            auto padded_img_gray = image_gray.Pad(1, 1, 1, 1, {0});
            REQUIRE(padded_img_gray.Width() == image_gray.Width() + 2);
            REQUIRE(padded_img_gray.Height() == image_gray.Height() + 2);
            REQUIRE_EQ_C_ARRAY(padded_img_gray.Data(), expected_pad_gray, 3 * 4 * 1);
        }

        SECTION("Test image basic operations") {
            auto image_full = image.Clone();
            image_full.Fill(23);
            for (int i = 0; i < 3 * 2 * 3; ++i) {
                REQUIRE(image_full.Data()[i] == 23);
            }

            auto image_mul = image.Mul(7);
            for (int i = 0; i < 3 * 2 * 3; ++i) {
                REQUIRE(image_mul.Data()[i] == 7 * data_uint8[i]);
            }

            auto image_add = image.Add(10);
            for (int i = 0; i < 3 * 2 * 3; ++i) {
                REQUIRE(image_add.Data()[i] == 10 + data_uint8[i]);
            }

            uint8_t expected_fill[] = {66, 66, 66, 4,  5,  6,  66, 66, 66,
                                       10, 11, 12, 13, 14, 15, 16, 17, 18};
            auto image_full_rect = image.Clone();
            auto rect = inspirecv::Rect<int>(0, 0, 1, 2);
            image_full_rect.Fill(rect, {66, 66, 66});
            REQUIRE_EQ_C_ARRAY(image_full_rect.Data(), expected_fill, 2 * 1 * 3);


            // gray
            auto image_gray_full = image_gray.Clone();
            image_gray_full.Fill(66);
            for (int i = 0; i < 2 * 1 * 1; ++i) {
                REQUIRE(image_gray_full.Data()[i] == 66);
            }

            auto image_gray_mul = image_gray.Mul(7);
            for (int i = 0; i < 2 * 1 * 1; ++i) {
                REQUIRE(image_gray_mul.Data()[i] == 7 * data_gray_uint8[i]);
            }

            auto image_gray_add = image_gray.Add(10);
            for (int i = 0; i < 2 * 1 * 1; ++i) {
                REQUIRE(image_gray_add.Data()[i] == 10 + data_gray_uint8[i]);
            }

            auto image_gray_full_rect = image_gray.Clone();
            uint8_t expected_fill_gray[] = {66, 2, 66, 4, 5, 6};
            image_gray_full_rect.Fill(rect, {66});
            REQUIRE_EQ_C_ARRAY(image_gray_full_rect.Data(), expected_fill_gray, 2 * 1 * 1);
        }
    }


    SECTION("Test image swap rb") {
      // 2 * 3 * 3
        uint8_t data_uint8[] = {
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        };
        auto image = inspirecv::Image::Create(2, 3, 3, data_uint8);
        uint8_t expected_swaprb[] = {3, 2, 1, 6, 5, 4, 9, 8, 7, 12, 11, 10, 15, 14, 13, 18, 17, 16};
        auto swapped_img = image.SwapRB();
        REQUIRE_EQ_C_ARRAY(swapped_img.Data(), expected_swaprb, 3 * 2 * 3);
    }
}
