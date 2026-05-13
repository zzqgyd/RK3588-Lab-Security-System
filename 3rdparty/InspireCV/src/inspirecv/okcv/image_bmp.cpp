
#include <cmath>
#include <string>
#include <utility>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <vector>
#include <limits>
#include <type_traits>
#include <thread>
#include <deque>

#if defined(__has_include)
#  if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && __has_include(<arm_neon.h>)
#    include <arm_neon.h>
#  endif
#  if (defined(__AVX2__) || defined(__SSE2__)) && __has_include(<immintrin.h>)
#    include <immintrin.h>
#  endif
#else
#  if defined(__ARM_NEON) || defined(__ARM_NEON__)
#    include <arm_neon.h>
#  endif
#  if defined(__AVX2__) || defined(__SSE2__)
#    include <immintrin.h>
#  endif
#endif

#include "image_bmp.h"
#include "check.h"

#ifndef INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO
#include "io/stb_warpper.h"
#endif

namespace okcv {

template <typename D>
Image<D>::Image(Image &&that) {
    data_ = std::move(that.data_);
    height_ = that.height_;
    width_ = that.width_;
    channels_ = that.channels_;

    that.height_ = 0;
    that.width_ = 0;
}

template <typename D>
Image<D> &Image<D>::operator=(Image &&that) {
    data_ = std::move(that.data_);
    height_ = that.height_;
    width_ = that.width_;
    channels_ = that.channels_;

    that.height_ = 0;
    that.width_ = 0;
    return *this;
}

template <typename D>
void Image<D>::Reset() {
    height_ = 0;
    width_ = 0;
    channels_ = 0;
    data_ = nullptr;
}

template <typename D>
void Image<D>::Reset(int width, int height, int channels, const D *data, bool copy_data) {
    if (data == nullptr || copy_data) {
        // Original owned data path
        int new_size = height * width * channels;
        if (DataSize() != new_size) {
            data_.reset(new D[new_size]);
        }
        height_ = height;
        width_ = width;
        channels_ = channels;
        is_external_ = false;
        external_data_ = nullptr;

        if (data != nullptr) {
            std::memcpy(data_.get(), data, sizeof(D) * new_size);
        }
    } else {
        // External data path
        data_.reset();  // Release any owned data
        external_data_ = data;
        is_external_ = true;
        height_ = height;
        width_ = width;
        channels_ = channels;
    }
}

template <typename D>
Image<D> Image<D>::Clone() const {
    Image<D> dst;
    dst.Reset(width_, height_, channels_, Data());
    return dst;
}

template <typename D>
void Image<D>::CopyTo(Image<D> &dst) const {
    if (this != &dst) {
        INSPIRECV_CHECK(!Empty());
        dst.Reset(width_, height_, channels_, Data());
    }
}

template <typename D>
void Image<D>::Read(const char *filename, int channels) {
#ifdef INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO
    INSPIRECV_CHECK(channels == 1 || channels == 3);
    int flag = channels == 3 ? cv::IMREAD_COLOR : cv::IMREAD_GRAYSCALE;
    cv::Mat image = cv::imread(filename, flag);
    INSPIRECV_CHECK(image.data) << "Could not open file " << filename;
    this->FromCVMat(image, false);
#else
    ImageReader::ImageData data;
    bool succ = ImageReader::Read(filename, data, channels, ImageReader::ColorOrder::BGR);
    INSPIRECV_CHECK(succ) << "Could not open file " << filename;
    // The default bgr is used as with opencv
    this->Reset(data.width, data.height, data.channels,
                reinterpret_cast<const D *>(data.data.data()));
#endif  // INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO
}

template <typename D>
void Image<D>::Read(const std::string &filename, int channels) {
    Read(filename.c_str(), channels);
}

template <typename D>
void Image<D>::Write(const char *filename) const {
#ifdef INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO
    cv::Mat mat;
    this->ToCVMat(mat, false);
    cv::imwrite(filename, mat);
#else
    ImageReader::WriteConfig config;
    config.jpg_quality = 100;
    config.png_compression = 9;
    config.color_order = ImageReader::ColorOrder::BGR;

    bool succ = ImageReader::Write(filename, reinterpret_cast<const unsigned char *>(Data()),
                                   Width(), Height(), Channels(), config);
    INSPIRECV_CHECK(succ) << "Could not write file " << filename;
#endif  // INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO
}

template <typename D>
void Image<D>::Write(const std::string &filename) const {
    Write(filename.c_str());
}

template <typename D>
void Image<D>::FromImageBuffer(const std::vector<char> &buffer, int channels) {
#ifdef INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO
    INSPIRECV_CHECK(channels == 1 || channels == 3);
    int flag = channels == 3 ? cv::IMREAD_COLOR : cv::IMREAD_GRAYSCALE;
    cv::Mat image = cv::imdecode(buffer, flag);
    INSPIRECV_CHECK(image.data) << "Decode image error!";
    this->FromCVMat(image, false);
#else
    INSPIRECV_LOG(FATAL) << "Not implemented okcv::Image::FromImageBuffer!";
#endif  // INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO
}

template <typename D>
void Image<D>::Show(const std::string &name, int time) const {
#ifdef INSPIRECV_BACKEND_OKCV_USE_OPENCV_GUI
    cv::Mat mat;
    this->ToCVMat(mat, false);
    mat.convertTo(mat, CV_8U);
    cv::imshow(name, mat);
    cv::waitKey(time);
#else
    INSPIRECV_LOG(FATAL) << "Not implemented okcv::Image::Show!";
#endif
}

template <typename D>
void Image<D>::Fill(D v) {
    std::fill_n(Data(), DataSize(), v);
}

template <typename D>
Image<D> Image<D>::Mul(float a) const {
    Image<D> res;
    res.Reset(width_, height_, channels_);
    auto res_iter = res.Data();
    for (int i = 0; i < DataSize(); ++i) {
        *res_iter++ = data_.get()[i] * a;
    }
    return res;
}

template <typename D>
Image<D> Image<D>::MulAdd(float a, float b) const {
    Image<D> res;
    res.Reset(width_, height_, channels_);
    auto res_iter = res.Data();
    for (int i = 0; i < DataSize(); ++i) {
        *res_iter++ = data_.get()[i] * a + b;
    }
    return res;
}

template <typename D>
Image<D> Image<D>::ElementWiseOperate(const Image<D> &image,
                                      const std::function<D(D, D)> &op) const {
    Image<D> res;
    INSPIRECV_CHECK(Width() == image.Width())
      << "width=" << Width() << ", image.width=" << image.Width();
    INSPIRECV_CHECK(Height() == image.Height())
      << "height=" << Height() << ", image.height=" << image.Height();
    res.Reset(width_, height_, channels_);
    auto res_iter = res.Data();
    auto iter1 = Data();
    auto iter2 = image.Data();
    for (int i = 0; i < DataSize(); ++i) {
        *res_iter = op(*iter1, *iter2);
        ++res_iter;
        ++iter1;
        ++iter2;
    }
    return res;
}

template <typename D>
void Image<D>::ApplyPixelwiseOperation(const std::function<D(D)> &func) {
    auto iter = Data();
    for (int i = 0; i < DataSize(); ++i) {
        *iter = func(*iter);
        ++iter;
    }
}

template <>
inline float Image<float>::InterpolateBilinear(const float top_left, const float top_right,
                                               const float bottom_left, const float bottom_right,
                                               const float x_lerp, const float y_lerp) const {
    const float top = top_left + (top_right - top_left) * x_lerp;
    const float bottom = bottom_left + (bottom_right - bottom_left) * x_lerp;
    return top + (bottom - top) * y_lerp;
}

template <>
inline uint8_t Image<uint8_t>::InterpolateBilinear(const uint8_t top_left, const uint8_t top_right,
                                                   const uint8_t bottom_left,
                                                   const uint8_t bottom_right, const float x_lerp,
                                                   const float y_lerp) const {
    const float top = top_left + (top_right - top_left) * x_lerp;
    const float bottom = bottom_left + (bottom_right - bottom_left) * x_lerp;
    return static_cast<uint8_t>(roundf(top + (bottom - top) * y_lerp));
}

template <>
inline void Image<uint8_t>::OutInterpolateBilinearFloatx4x1Neon(
  const uint8_t *top_left_0, const uint8_t *top_left_1, const uint8_t *top_left_2,
  const uint8_t *top_left_3, const uint8_t *top_right_0, const uint8_t *top_right_1,
  const uint8_t *top_right_2, const uint8_t *top_right_3, const uint8_t *bottom_left_0,
  const uint8_t *bottom_left_1, const uint8_t *bottom_left_2, const uint8_t *bottom_left_3,
  const uint8_t *bottom_right_0, const uint8_t *bottom_right_1, const uint8_t *bottom_right_2,
  const uint8_t *bottom_right_3, const float *x_lerp, const float *y_lerp, uint8_t *out) const {
    INSPIRECV_LOG(ERROR) << "OutComputeLerpFloatx4x1Neon no support";
}

template <>
inline void Image<float>::OutInterpolateBilinearFloatx4x1Neon(
  const float *top_left_0, const float *top_left_1, const float *top_left_2,
  const float *top_left_3, const float *top_right_0, const float *top_right_1,
  const float *top_right_2, const float *top_right_3, const float *bottom_left_0,
  const float *bottom_left_1, const float *bottom_left_2, const float *bottom_left_3,
  const float *bottom_right_0, const float *bottom_right_1, const float *bottom_right_2,
  const float *bottom_right_3, const float *x_lerp, const float *y_lerp, float *out) const {
#if (defined(__ARM_NEON__) || defined(__ARM_NEON))
    static const float32x4_t v_zero = vmovq_n_f32(0.0);
    float32x4_t v_top_left, v_top_right, v_bottom_left, v_bottom_right;
    float32x4_t v_x_lerp = vld1q_f32(x_lerp);
    float32x4_t v_y_lerp = vld1q_f32(y_lerp);
    v_top_left = vld1q_lane_f32(top_left_0, v_zero, 0);
    v_top_left = vld1q_lane_f32(top_left_1, v_top_left, 1);
    v_top_left = vld1q_lane_f32(top_left_2, v_top_left, 2);
    v_top_left = vld1q_lane_f32(top_left_3, v_top_left, 3);

    v_top_right = vld1q_lane_f32(top_right_0, v_zero, 0);
    v_top_right = vld1q_lane_f32(top_right_1, v_top_right, 1);
    v_top_right = vld1q_lane_f32(top_right_2, v_top_right, 2);
    v_top_right = vld1q_lane_f32(top_right_3, v_top_right, 3);

    v_bottom_left = vld1q_lane_f32(bottom_left_0, v_zero, 0);
    v_bottom_left = vld1q_lane_f32(bottom_left_1, v_bottom_left, 1);
    v_bottom_left = vld1q_lane_f32(bottom_left_2, v_bottom_left, 2);
    v_bottom_left = vld1q_lane_f32(bottom_left_3, v_bottom_left, 3);

    v_bottom_right = vld1q_lane_f32(bottom_right_0, v_zero, 0);
    v_bottom_right = vld1q_lane_f32(bottom_right_1, v_bottom_right, 1);
    v_bottom_right = vld1q_lane_f32(bottom_right_2, v_bottom_right, 2);
    v_bottom_right = vld1q_lane_f32(bottom_right_3, v_bottom_right, 3);

    float32x4_t v_sub = vsubq_f32(v_top_right, v_top_left);
    float32x4_t v_top = vaddq_f32(v_top_left, vmulq_f32(v_sub, v_x_lerp));
    float32x4_t v_sub_b = vsubq_f32(v_bottom_right, v_bottom_left);
    float32x4_t v_bottom = vaddq_f32(v_bottom_left, vmulq_f32(v_sub_b, v_x_lerp));
    float32x4_t v_ret_sub = vsubq_f32(v_bottom, v_top);
    float32x4_t v_ret = vaddq_f32(v_top, vmulq_f32(v_ret_sub, v_y_lerp));

    vst1q_f32(out, v_ret);
#endif
    return;
}

template <typename D>
Image<D> Image<D>::ResizeBilinear(int width, int height) const {
    INSPIRECV_CHECK(height > 0 && width > 0) << "height=" << height << ", width=" << width;
    if (height_ == height && width_ == width) {
        return Clone();
    }

    Image<D> dst;
    dst.Reset(width, height, channels_);
    const float height_scale = static_cast<float>(height_) / height;
    const float width_scale = static_cast<float>(width_) / width;
    std::vector<int> in_x_low(width);
    std::vector<int> in_x_up(width);
    std::vector<float> lerp_x(width);
    for (int x = 0; x < width; ++x) {
        float in_x = x * width_scale;
        in_x_low[x] = std::min(static_cast<int>(in_x), width_ - 1);
        in_x_up[x] = std::min(in_x_low[x] + 1, width_ - 1);
        lerp_x[x] = in_x - in_x_low[x];
    }

    auto dst_iter = dst.Data();
    for (int y = 0; y < height; ++y) {
        float in_y = y * height_scale;
        int in_y_low = std::min(static_cast<int>(in_y), height_ - 1);
        int in_y_up = std::min(in_y_low + 1, height_ - 1);
        float lerp_y = in_y - in_y_low;
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < channels_; ++c) {
                *dst_iter++ = InterpolateBilinear(
                  at(in_y_low, in_x_low[x])[c], at(in_y_low, in_x_up[x])[c],
                  at(in_y_up, in_x_low[x])[c], at(in_y_up, in_x_up[x])[c], lerp_x[x], lerp_y);
            }
        }
    }

    return dst;
}

template <typename D>
Image<D> Image<D>::ResizeNearest(int width, int height) const {
    INSPIRECV_CHECK(height > 0 && width > 0) << "height=" << height << ", width=" << width;
    if (height_ == height && width_ == width) {
        return Clone();
    }

    Image<D> dst;
    dst.Reset(width, height, channels_);
    const float height_scale = static_cast<float>(height_) / height;
    const float width_scale = static_cast<float>(width_) / width;
    for (int y = 0; y < height; ++y) {
        int in_y = std::min(static_cast<int>(y * height_scale), height_ - 1);
        for (int x = 0; x < width; ++x) {
            int in_x = std::min(static_cast<int>(x * width_scale), width_ - 1);
            std::memcpy(dst.at(y, x), at(in_y, in_x), sizeof(D) * channels_);
        }
    }

    return dst;
}

template <typename D>
void Image<D>::CropAndResizeNearest(Image<D> &dst, const Rect<int> &rect, int resize_width,
                                    int resize_height) const {
    INSPIRECV_CHECK(this != &dst);
    INSPIRECV_CHECK(resize_height > 0 && resize_width > 0)
      << ", resize_height=" << resize_height << ", resize_width=" << resize_width;
    INSPIRECV_CHECK(Rect<int>(0, 0, width_, height_).Contains(rect)) << rect;

    dst.Reset(resize_width, resize_height, channels_);
    const float height_scale = static_cast<float>(rect.GetHeight()) / resize_height;
    const float width_scale = static_cast<float>(rect.GetWidth()) / resize_width;
    for (int y = 0; y < resize_height; ++y) {
        int in_y = std::min(static_cast<int>(y * height_scale), rect.GetHeight() - 1) + rect.ymin();
        for (int x = 0; x < resize_width; ++x) {
            int in_x =
              std::min(static_cast<int>(x * width_scale), rect.GetWidth() - 1) + rect.xmin();
            std::memcpy(dst.at(y, x), at(in_y, in_x), sizeof(D) * channels_);
        }
    }
}

template <typename D>
void Image<D>::CropAndResizeBilinear(Image<D> &dst, const Rect<int> &rect, int resize_width,
                                     int resize_height) const {
    INSPIRECV_CHECK(this != &dst);
    INSPIRECV_CHECK(resize_height > 0 && resize_width > 0)
      << ", resize_height=" << resize_height << ", resize_width=" << resize_width;
    INSPIRECV_CHECK(Rect<int>(0, 0, width_, height_).Contains(rect)) << rect;

    dst.Reset(resize_width, resize_height, channels_);
    const float height_scale = static_cast<float>(rect.GetHeight()) / resize_height;
    const float width_scale = static_cast<float>(rect.GetWidth()) / resize_width;

    std::vector<int> in_x_low(resize_width);
    std::vector<int> in_x_up(resize_width);
    std::vector<float> lerp_x(resize_width);
    for (int x = 0; x < resize_width; ++x) {
        float in_x = x * width_scale + rect.xmin();
        in_x_low[x] = std::min(static_cast<int>(in_x), width_ - 1);
        in_x_up[x] = std::min(in_x_low[x] + 1, width_ - 1);
        lerp_x[x] = in_x - in_x_low[x];
    }

    auto dst_iter = dst.Data();
    for (int y = 0; y < resize_height; ++y) {
        float in_y = y * height_scale + rect.ymin();
        int in_y_low = std::min(static_cast<int>(in_y), height_ - 1);
        int in_y_up = std::min(in_y_low + 1, height_ - 1);
        float lerp_y = in_y - in_y_low;
        for (int x = 0; x < resize_width; ++x) {
            for (int c = 0; c < channels_; ++c) {
                *(dst_iter++) = InterpolateBilinear(
                  at(in_y_low, in_x_low[x])[c], at(in_y_low, in_x_up[x])[c],
                  at(in_y_up, in_x_low[x])[c], at(in_y_up, in_x_up[x])[c], lerp_x[x], lerp_y);
            }
        }
    }
}

template <typename D>
void Image<D>::GetTransformMatrix(int width, int height, const Rect<int> &rect,
                                  TransformMatrix &matrix) const {
    matrix[0] = static_cast<float>(rect.GetWidth()) / width;
    matrix[1] = 0;
    matrix[2] = rect.xmin();
    matrix[3] = 0;
    matrix[4] = static_cast<float>(rect.GetHeight()) / height;
    matrix[5] = rect.ymin();
}

template <typename D>
Image<D> Image<D>::AffineBilinearReference(int width, int height, const TransformMatrix &matrix,
                                           BorderMode border_mode, D border_value) const {
    Image<D> dst;
    dst.Reset(width, height, channels_);
    dst.Fill(0);
    auto dst_iter = dst.Data();
    for (int dst_y = 0; dst_y < height; ++dst_y) {
        for (int dst_x = 0; dst_x < width; ++dst_x) {
            float src_x = dst_x * matrix[0] + dst_y * matrix[1] + matrix[2];
            float src_y = dst_x * matrix[3] + dst_y * matrix[4] + matrix[5];

            if (src_x >= width_ || src_y >= height_ || src_x < 0 || src_y < 0) {
                if (border_mode == BORDER_MODE_CONSTANT) {
                    for (int c = 0; c < channels_; ++c)
                        *(dst_iter++) = border_value;
                    continue;
                } else if (border_mode == BORDER_MODE_REPLICATE) {
                    if (src_x >= width_) {
                        src_x = width_ - 1;
                    }
                    if (src_y >= height_) {
                        src_y = height_ - 1;
                    }
                    if (src_x < 0) {
                        src_x = 0;
                    }
                    if (src_y < 0) {
                        src_y = 0;
                    }
                } else {
                    INSPIRECV_LOG(ERROR) << "unsupport border mode:" << border_mode;
                }
            }

            int src_x_low = std::min(static_cast<int>(src_x), width_ - 1);
            int src_x_up = std::min(src_x_low + 1, width_ - 1);
            float lerp_x = src_x - src_x_low;
            int src_y_low = std::min(static_cast<int>(src_y), height_ - 1);
            int src_y_up = std::min(src_y_low + 1, height_ - 1);
            float lerp_y = src_y - src_y_low;

            for (int c = 0; c < channels_; ++c) {
                *(dst_iter++) = InterpolateBilinear(
                  at(src_y_low, src_x_low)[c], at(src_y_low, src_x_up)[c],
                  at(src_y_up, src_x_low)[c], at(src_y_up, src_x_up)[c], lerp_x, lerp_y);
            }
        }
    }

    return dst;
}

template <>
Image<float> Image<float>::AffineBilinear(int width, int height, const TransformMatrix &matrix,
                                          BorderMode border_mode, float border_value) const {
    Image<float> dst;
    dst.Reset(width, height, channels_);
    float *dst_iter = dst.Data();

    if (channels_ != 1) {
        return AffineBilinearReference(width, height, matrix);
    }
    if ((!(fabs(matrix[0]) < 1e-3 && fabs(matrix[4]) < 1e-3)) &&
        (!(fabs(matrix[1]) < 1e-3 && fabs(matrix[3]) < 1e-3))) {
        return AffineBilinearReference(width, height, matrix);
    }

    struct AffineIndex {
        int src_low;
        int src_up;
        float lerp;
        bool overstep_lower;
    };

    if (fabs(matrix[0]) < 1e-3 && fabs(matrix[4]) < 1e-3) {
        std::vector<AffineIndex> affine_index(height + width);
        AffineIndex *affine_index_x = &affine_index[0];
        AffineIndex *affine_index_y = &affine_index[height];

        for (int dst_y = 0; dst_y < height; ++dst_y) {
            float src_x = dst_y * matrix[1] + matrix[2];
            if (src_x >= width_ || src_x < 0) {
                if (border_mode == BORDER_MODE_CONSTANT) {
                    affine_index_x[dst_y].src_low = -1;
                } else if (border_mode == BORDER_MODE_REPLICATE) {
                    if (src_x >= width_) {
                        src_x = width_ - 1;
                    }
                    if (src_x < 0) {
                        src_x = 0;
                    }
                } else {
                    INSPIRECV_LOG(ERROR) << "unsupport border mode:" << border_mode;
                }
            } else {
                int src_x_low = std::min(static_cast<int>(src_x), width_ - 1);
                int src_x_up = std::min(src_x_low + 1, width_ - 1);
                float lerp_x = src_x - src_x_low;
                affine_index_x[dst_y].src_low = src_x_low;
                affine_index_x[dst_y].src_up = src_x_up;
                affine_index_x[dst_y].lerp = lerp_x;
            }
        }

        for (int dst_x = 0; dst_x < width; ++dst_x) {
            float src_y = dst_x * matrix[3] + matrix[5];
            if (src_y >= height_ || src_y < 0) {
                if (border_mode == BORDER_MODE_CONSTANT) {
                    affine_index_y[dst_x].src_low = -1;
                } else if (border_mode == BORDER_MODE_REPLICATE) {
                    if (src_y >= height_) {
                        src_y = height_ - 1;
                    }
                    if (src_y < 0) {
                        src_y = 0;
                    }
                    int src_y_low = std::min(static_cast<int>(src_y), height_ - 1);
                    int src_y_up = std::min(src_y_low + 1, height_ - 1);
                    float lerp_y = src_y - src_y_low;
                    affine_index_y[dst_x].src_low = src_y_low;
                    affine_index_y[dst_x].src_up = src_y_up;
                    affine_index_y[dst_x].lerp = lerp_y;
                } else {
                    INSPIRECV_LOG(ERROR) << "unsupport border mode:" << border_mode;
                }
            } else {
                int src_y_low = std::min(static_cast<int>(src_y), height_ - 1);
                int src_y_up = std::min(src_y_low + 1, height_ - 1);
                float lerp_y = src_y - src_y_low;
                affine_index_y[dst_x].src_low = src_y_low;
                affine_index_y[dst_x].src_up = src_y_up;
                affine_index_y[dst_x].lerp = lerp_y;
            }
        }
        int dst_y = 0;
        if (border_mode == BORDER_MODE_CONSTANT) {
            for (; dst_y <= height - 4; dst_y += 4) {
                if (affine_index_x[dst_y].src_low == -1 ||
                    affine_index_x[dst_y + 1].src_low == -1 ||
                    affine_index_x[dst_y + 2].src_low == -1 ||
                    affine_index_x[dst_y + 3].src_low == -1) {
                    affine_index_x[dst_y / 4].overstep_lower = true;
                } else {
                    affine_index_x[dst_y / 4].overstep_lower = false;
                }
            }
            affine_index_x[dst_y].overstep_lower = false;
            for (; dst_y < height; dst_y++) {
                if (affine_index_x[dst_y].src_low == -1) {
                    affine_index_x[dst_y / 4].overstep_lower = true;
                    break;
                }
            }
        }

        dst_y = 0;
#if (defined(__ARM_NEON__) || defined(__ARM_NEON))
        for (; dst_y <= height - 4; dst_y += 4) {
            for (int dst_x = 0; dst_x < width; dst_x++) {
                const float *top_left_0, *top_left_1, *top_left_2, *top_left_3;
                const float *top_right_0, *top_right_1, *top_right_2, *top_right_3;
                const float *bottom_left_0, *bottom_left_1, *bottom_left_2, *bottom_left_3;
                const float *bottom_right_0, *bottom_right_1, *bottom_right_2, *bottom_right_3;
                float x_lerp[4];
                float y_lerp[4];
                if (border_mode == BORDER_MODE_CONSTANT &&
                    (affine_index_y[dst_x].src_low == -1 ||
                     affine_index_x[dst_y / 4].overstep_lower == true)) {
                    for (int dst_y_tmp = dst_y; dst_y_tmp < dst_y + 4; ++dst_y_tmp) {
                        if (affine_index_x[dst_y_tmp].src_low == -1 ||
                            affine_index_y[dst_x].src_low == -1) {
                            dst_iter[dst_y_tmp * width + dst_x] = border_value;
                        } else {
                            dst_iter[dst_y_tmp * width + dst_x] = InterpolateBilinear(
                              at(affine_index_y[dst_x].src_low,
                                 affine_index_x[dst_y_tmp].src_low)[0],
                              at(affine_index_y[dst_x].src_low,
                                 affine_index_x[dst_y_tmp].src_up)[0],
                              at(affine_index_y[dst_x].src_up,
                                 affine_index_x[dst_y_tmp].src_low)[0],
                              at(affine_index_y[dst_x].src_up, affine_index_x[dst_y_tmp].src_up)[0],
                              affine_index_x[dst_y_tmp].lerp, affine_index_y[dst_x].lerp);
                        }
                    }
                } else {
                    struct AffineIndex *affine_index_local_x = affine_index_x + dst_y;
                    struct AffineIndex *affine_index_local_y = affine_index_y + dst_x;

                    top_left_0 = Data() + affine_index_local_y->src_low * width_ +
                                 affine_index_local_x->src_low;
                    top_left_1 = Data() + affine_index_local_y->src_low * width_ +
                                 (affine_index_local_x + 1)->src_low;
                    top_left_2 = Data() + affine_index_local_y->src_low * width_ +
                                 (affine_index_local_x + 2)->src_low;
                    top_left_3 = Data() + affine_index_local_y->src_low * width_ +
                                 (affine_index_local_x + 3)->src_low;

                    top_right_0 = Data() + affine_index_local_y->src_low * width_ +
                                  affine_index_local_x->src_up;
                    top_right_1 = Data() + affine_index_local_y->src_low * width_ +
                                  (affine_index_local_x + 1)->src_up;
                    top_right_2 = Data() + affine_index_local_y->src_low * width_ +
                                  (affine_index_local_x + 2)->src_up;
                    top_right_3 = Data() + affine_index_local_y->src_low * width_ +
                                  (affine_index_local_x + 3)->src_up;

                    bottom_left_0 = Data() + affine_index_local_y->src_up * width_ +
                                    affine_index_local_x->src_low;
                    bottom_left_1 = Data() + affine_index_local_y->src_up * width_ +
                                    (affine_index_local_x + 1)->src_low;
                    bottom_left_2 = Data() + affine_index_local_y->src_up * width_ +
                                    (affine_index_local_x + 2)->src_low;
                    bottom_left_3 = Data() + affine_index_local_y->src_up * width_ +
                                    (affine_index_local_x + 3)->src_low;

                    bottom_right_0 =
                      Data() + affine_index_local_y->src_up * width_ + affine_index_local_x->src_up;
                    bottom_right_1 = Data() + affine_index_local_y->src_up * width_ +
                                     (affine_index_local_x + 1)->src_up;
                    bottom_right_2 = Data() + affine_index_local_y->src_up * width_ +
                                     (affine_index_local_x + 2)->src_up;
                    bottom_right_3 = Data() + affine_index_local_y->src_up * width_ +
                                     (affine_index_local_x + 3)->src_up;
                    x_lerp[0] = affine_index_local_x->lerp;
                    x_lerp[1] = (affine_index_local_x + 1)->lerp;
                    x_lerp[2] = (affine_index_local_x + 2)->lerp;
                    x_lerp[3] = (affine_index_local_x + 3)->lerp;
                    y_lerp[0] = affine_index_local_y->lerp;
                    y_lerp[1] = affine_index_local_y->lerp;
                    y_lerp[2] = affine_index_local_y->lerp;
                    y_lerp[3] = affine_index_local_y->lerp;

                    float out[4];

                    OutInterpolateBilinearFloatx4x1Neon(
                      top_left_0, top_left_1, top_left_2, top_left_3, top_right_0, top_right_1,
                      top_right_2, top_right_3, bottom_left_0, bottom_left_1, bottom_left_2,
                      bottom_left_3, bottom_right_0, bottom_right_1, bottom_right_2, bottom_right_3,
                      x_lerp, y_lerp, out);
                    dst_iter[dst_y * width + dst_x] = out[0];
                    dst_iter[(dst_y + 1) * width + dst_x] = out[1];
                    dst_iter[(dst_y + 2) * width + dst_x] = out[2];
                    dst_iter[(dst_y + 3) * width + dst_x] = out[3];
                }
            }
        }
#endif
        for (; dst_y < height; ++dst_y) {
            for (int dst_x = 0; dst_x < width; dst_x++) {
                if (affine_index_x[dst_y].src_low == -1 || affine_index_y[dst_x].src_low == -1) {
                    dst_iter[dst_y * width + dst_x] = border_value;
                } else {
                    dst_iter[dst_y * width + dst_x] = InterpolateBilinear(
                      at(affine_index_y[dst_x].src_low, affine_index_x[dst_y].src_low)[0],
                      at(affine_index_y[dst_x].src_low, affine_index_x[dst_y].src_up)[0],
                      at(affine_index_y[dst_x].src_up, affine_index_x[dst_y].src_low)[0],
                      at(affine_index_y[dst_x].src_up, affine_index_x[dst_y].src_up)[0],
                      affine_index_x[dst_y].lerp, affine_index_y[dst_x].lerp);
                }
            }
        }
    } else if (fabs(matrix[1]) < 1e-3 && fabs(matrix[3]) < 1e-3) {
        std::vector<AffineIndex> affine_index(width + height);
        AffineIndex *affine_index_x = &affine_index[0];
        AffineIndex *affine_index_y = &affine_index[width];

        for (int dst_y = 0; dst_y < height; ++dst_y) {
            float src_y = dst_y * matrix[4] + matrix[5];
            if (src_y >= height_ || src_y < 0) {
                if (border_mode == BORDER_MODE_CONSTANT) {
                    affine_index_y[dst_y].src_low = -1;
                } else if (border_mode == BORDER_MODE_REPLICATE) {
                    if (src_y >= height_) {
                        src_y = height_ - 1;
                    }
                    if (src_y < 0) {
                        src_y = 0;
                    }
                } else {
                    INSPIRECV_LOG(ERROR) << "unsupport border mode:" << border_mode;
                }
            } else {
                int src_y_low = std::min(static_cast<int>(src_y), height_ - 1);
                int src_y_up = std::min(src_y_low + 1, height_ - 1);
                float lerp_y = src_y - src_y_low;
                affine_index_y[dst_y].src_low = src_y_low;
                affine_index_y[dst_y].src_up = src_y_up;
                affine_index_y[dst_y].lerp = lerp_y;
            }
        }
        for (int dst_x = 0; dst_x < width; ++dst_x) {
            float src_x = dst_x * matrix[0] + matrix[2];
            if (src_x >= width_ || src_x < 0) {
                if (border_mode == BORDER_MODE_CONSTANT) {
                    affine_index_x[dst_x].src_low = -1;
                } else if (border_mode == BORDER_MODE_REPLICATE) {
                    if (src_x >= width_) {
                        src_x = width_ - 1;
                    }
                    if (src_x < 0) {
                        src_x = 0;
                    }
                    int src_x_low = std::min(static_cast<int>(src_x), width_ - 1);
                    int src_x_up = std::min(src_x_low + 1, width_ - 1);
                    float lerp_x = src_x - src_x_low;
                    affine_index_x[dst_x].src_low = src_x_low;
                    affine_index_x[dst_x].src_up = src_x_up;
                    affine_index_x[dst_x].lerp = lerp_x;
                } else {
                    INSPIRECV_LOG(ERROR) << "unsupport border mode:" << border_mode;
                }
            } else {
                int src_x_low = std::min(static_cast<int>(src_x), width_ - 1);
                int src_x_up = std::min(src_x_low + 1, width_ - 1);
                float lerp_x = src_x - src_x_low;
                affine_index_x[dst_x].src_low = src_x_low;
                affine_index_x[dst_x].src_up = src_x_up;
                affine_index_x[dst_x].lerp = lerp_x;
            }
        }

        int dst_x = 0;
        if (border_mode == BORDER_MODE_CONSTANT) {
            for (; dst_x <= width - 4; dst_x += 4) {
                if (affine_index_x[dst_x].src_low == -1 ||
                    affine_index_x[dst_x + 1].src_low == -1 ||
                    affine_index_x[dst_x + 2].src_low == -1 ||
                    affine_index_x[dst_x + 3].src_low == -1) {
                    affine_index_x[dst_x / 4].overstep_lower = true;
                } else {
                    affine_index_x[dst_x / 4].overstep_lower = false;
                }
            }
            affine_index_x[dst_x / 4].overstep_lower = false;
            for (; dst_x < width; dst_x++) {
                if (affine_index_x[dst_x].src_low == -1) {
                    affine_index_x[dst_x / 4].overstep_lower = true;
                    break;
                }
            }
        }

        for (int dst_y = 0; dst_y < height; ++dst_y) {
            int dst_x = 0;
#if (defined(__ARM_NEON__) || defined(__ARM_NEON))
            for (; dst_x <= width - 4; dst_x += 4) {
                const float *top_left_0, *top_left_1, *top_left_2, *top_left_3;
                const float *top_right_0, *top_right_1, *top_right_2, *top_right_3;
                const float *bottom_left_0, *bottom_left_1, *bottom_left_2, *bottom_left_3;
                const float *bottom_right_0, *bottom_right_1, *bottom_right_2, *bottom_right_3;
                float x_lerp[4];
                float y_lerp[4];
                if (border_mode == BORDER_MODE_CONSTANT &&
                    (affine_index_y[dst_y].src_low == -1 ||
                     affine_index_x[dst_x / 4].overstep_lower == true)) {
                    for (int dst_x_tmp = dst_x; dst_x_tmp < dst_x + 4; ++dst_x_tmp) {
                        if (affine_index_y[dst_y].src_low == -1 ||
                            affine_index_x[dst_x_tmp].src_low == -1) {
                            *dst_iter++ = border_value;
                        } else {
                            *(dst_iter++) = InterpolateBilinear(
                              at(affine_index_y[dst_y].src_low,
                                 affine_index_x[dst_x_tmp].src_low)[0],
                              at(affine_index_y[dst_y].src_low,
                                 affine_index_x[dst_x_tmp].src_up)[0],
                              at(affine_index_y[dst_y].src_up,
                                 affine_index_x[dst_x_tmp].src_low)[0],
                              at(affine_index_y[dst_y].src_up, affine_index_x[dst_x_tmp].src_up)[0],
                              affine_index_x[dst_x_tmp].lerp, affine_index_y[dst_y].lerp);
                        }
                    }
                } else {
                    top_left_0 = Data() + affine_index_y[dst_y].src_low * width_ +
                                 affine_index_x[dst_x].src_low;
                    top_left_1 = Data() + affine_index_y[dst_y].src_low * width_ +
                                 affine_index_x[dst_x + 1].src_low;
                    top_left_2 = Data() + affine_index_y[dst_y].src_low * width_ +
                                 affine_index_x[dst_x + 2].src_low;
                    top_left_3 = Data() + affine_index_y[dst_y].src_low * width_ +
                                 affine_index_x[dst_x + 3].src_low;

                    top_right_0 = Data() + affine_index_y[dst_y].src_low * width_ +
                                  affine_index_x[dst_x].src_up;
                    top_right_1 = Data() + affine_index_y[dst_y].src_low * width_ +
                                  affine_index_x[dst_x + 1].src_up;
                    top_right_2 = Data() + affine_index_y[dst_y].src_low * width_ +
                                  affine_index_x[dst_x + 2].src_up;
                    top_right_3 = Data() + affine_index_y[dst_y].src_low * width_ +
                                  affine_index_x[dst_x + 3].src_up;

                    bottom_left_0 = Data() + affine_index_y[dst_y].src_up * width_ +
                                    affine_index_x[dst_x].src_low;
                    bottom_left_1 = Data() + affine_index_y[dst_y].src_up * width_ +
                                    affine_index_x[dst_x + 1].src_low;
                    bottom_left_2 = Data() + affine_index_y[dst_y].src_up * width_ +
                                    affine_index_x[dst_x + 2].src_low;
                    bottom_left_3 = Data() + affine_index_y[dst_y].src_up * width_ +
                                    affine_index_x[dst_x + 3].src_low;

                    bottom_right_0 =
                      Data() + affine_index_y[dst_y].src_up * width_ + affine_index_x[dst_x].src_up;
                    bottom_right_1 = Data() + affine_index_y[dst_y].src_up * width_ +
                                     affine_index_x[dst_x + 1].src_up;
                    bottom_right_2 = Data() + affine_index_y[dst_y].src_up * width_ +
                                     affine_index_x[dst_x + 2].src_up;
                    bottom_right_3 = Data() + affine_index_y[dst_y].src_up * width_ +
                                     affine_index_x[dst_x + 3].src_up;
                    x_lerp[0] = affine_index_x[dst_x].lerp;
                    x_lerp[1] = affine_index_x[dst_x + 1].lerp;
                    x_lerp[2] = affine_index_x[dst_x + 2].lerp;
                    x_lerp[3] = affine_index_x[dst_x + 3].lerp;
                    y_lerp[0] = y_lerp[1] = y_lerp[2] = y_lerp[3] = affine_index_y[dst_y].lerp;

                    OutInterpolateBilinearFloatx4x1Neon(
                      top_left_0, top_left_1, top_left_2, top_left_3, top_right_0, top_right_1,
                      top_right_2, top_right_3, bottom_left_0, bottom_left_1, bottom_left_2,
                      bottom_left_3, bottom_right_0, bottom_right_1, bottom_right_2, bottom_right_3,
                      x_lerp, y_lerp, dst_iter);
                    dst_iter += 4;
                }
            }
#endif
            for (; dst_x < width; dst_x++) {
                if (affine_index_x[dst_x].src_low == -1 || affine_index_y[dst_y].src_low == -1) {
                    *(dst_iter++) = border_value;
                } else {
                    *(dst_iter++) = InterpolateBilinear(
                      at(affine_index_y[dst_y].src_low, affine_index_x[dst_x].src_low)[0],
                      at(affine_index_y[dst_y].src_low, affine_index_x[dst_x].src_up)[0],
                      at(affine_index_y[dst_y].src_up, affine_index_x[dst_x].src_low)[0],
                      at(affine_index_y[dst_y].src_up, affine_index_x[dst_x].src_up)[0],
                      affine_index_x[dst_x].lerp, affine_index_y[dst_y].lerp);
                }
            }
        }
    }

    return dst;
}

template <typename D>
Image<D> Image<D>::AffineBilinear(int width, int height, const TransformMatrix &matrix,
                                  BorderMode border_mode, D border_value) const {
    return AffineBilinearReference(width, height, matrix, border_mode, border_value);
}

template <typename D>
Image<D> Image<D>::Pad(int top, int down, int left, int right, D value) const {
    Image<D> dst;
    dst.Reset(width_ + left + right, height_ + top + down, channels_);
    dst.Fill(value);
    for (int i = 0; i < height_; ++i) {
        std::memcpy(dst.at(i + top, left), Row(i), sizeof(D) * width_ * channels_);
    }
    return dst;
}

template <typename D>
void Image<D>::AddAlphaChannel(Image<D> &dst, int index, D alpha) const {
    INSPIRECV_CHECK(this != &dst);
    INSPIRECV_CHECK(channels_ == 3) << "channels_=" << channels_;
    INSPIRECV_CHECK(index == 0 || index == channels_);
    dst.Reset(width_, height_, channels_ + 1);
    auto src_iter = Data();
    auto dst_iter = dst.Data();
    for (int i = 0; i < height_ * width_; ++i) {
        if (index == 0)
            *(dst_iter++) = alpha;
        std::memcpy(dst_iter, src_iter, sizeof(D) * channels_);
        dst_iter += channels_;
        src_iter += channels_;
        if (index == channels_)
            *(dst_iter++) = alpha;
    }
}

template <typename D>
Image<D> Image<D>::Crop(const Rect<int> &rect, bool allow_padding) const {
    Image<D> dst;
    if (allow_padding) {
        if (rect.ymin() >= height_ || rect.xmin() >= width_ || rect.ymax() <= 0 ||
            rect.xmax() <= 0) {
            dst.Reset(rect.GetWidth(), rect.GetHeight(), channels_);
            dst.Fill(0);
        } else {
            int src_top = std::max(rect.ymin(), 0);
            int src_left = std::max(rect.xmin(), 0);
            int src_height = std::min(rect.ymax(), height_) - src_top;
            int src_width = std::min(rect.xmax(), width_) - src_left;
            int dst_top = std::max(-rect.ymin(), 0);
            int dst_left = std::max(-rect.xmin(), 0);
            dst.Reset(rect.GetWidth(), rect.GetHeight(), channels_);
            dst.Fill(0);
            for (int i = 0; i < src_height; ++i) {
                std::memcpy(dst.at(dst_top + i, dst_left), at(src_top + i, src_left),
                            sizeof(D) * src_width * channels_);
            }
        }
    } else {
        INSPIRECV_CHECK(Rect<int>(0, 0, width_, height_).Contains(rect)) << rect;
        auto height = rect.GetHeight();
        auto width = rect.GetWidth();
        dst.Reset(width, height, channels_);
        for (int i = 0; i < height; ++i) {
            std::memcpy(dst.Row(i), this->at(i + rect.ymin(), rect.xmin()),
                        sizeof(D) * width * channels_);
        }
    }
    return dst;
}

template <typename D>
Image<D> Image<D>::Blur(int kernel) const {
    // WARN: the code is to be optimized.
    if (kernel == 1)
        return Clone();
    Image<D> res;
    if (kernel < 5) {
        Image<D> row_blur;
        row_blur.Reset(width_, height_, channels_);
        for (int c = 0; c < Channels(); ++c) {
            for (int i = 0; i < Height(); ++i) {
                auto r = Row(i);
                for (int j = 0; j < Width(); ++j) {
                    int min_k = std::max(0, j - (kernel - 1) / 2);
                    int max_k = std::min(Width() - 1, j + kernel / 2);
                    float s = 0;
                    for (int k = min_k; k <= max_k; ++k) {
                        s += r[k * Channels() + c];
                    }
                    row_blur.at(i, j)[c] = s / (max_k - min_k + 1);
                }
            }
        }

        res.Reset(width_, height_, channels_);
        for (int c = 0; c < Channels(); ++c) {
            for (int i = 0; i < Height(); ++i) {
                for (int j = 0; j < Width(); ++j) {
                    int min_k = std::max(0, i - (kernel - 1) / 2);
                    int max_k = std::min(Height() - 1, i + kernel / 2);
                    float s = 0;
                    for (int k = min_k; k <= max_k; ++k) {
                        s += row_blur.at(k, j)[c];
                    }
                    res.at(i, j)[c] = s / (max_k - min_k + 1);
                }
            }
        }
    } else {
        INSPIRECV_CHECK(channels_ == 1) << "channels: " << channels_;
        Image<float> sum;
        sum.Reset(Width(), Height(), Channels());
        for (int i = 0; i < DataSize(); ++i)
            sum.Data()[i] = Data()[i];
        for (int i = 0; i < DataSize(); ++i)
            if (i % Width() != 0)
                sum.Data()[i] += sum.Data()[i - 1];
        for (int i = Width(); i < DataSize(); ++i)
            sum.Data()[i] += sum.Data()[i - Width()];

        res.Reset(Width(), Height(), Channels());
        for (int y = 0; y < Height(); ++y)
            for (int x = 0; x < Width(); ++x) {
                int x1 = x - kernel / 2 - 1;
                int x2 = x + (kernel - 1) / 2;
                int y1 = y - kernel / 2 - 1;
                int y2 = y + (kernel - 1) / 2;
                float v11 = (x1 < 0 || y1 < 0) ? 0 : sum.at(y1, x1)[0];
                float v12 = x1 < 0 ? 0 : sum.at(std::min(y2, Height() - 1), x1)[0];
                float v21 = y1 < 0 ? 0 : sum.at(y1, std::min(x2, Width() - 1))[0];
                float v22 = sum.at(std::min(y2, Height() - 1), std::min(x2, Width() - 1))[0];
                int count_x = std::min(x2, Width() - 1) - std::max(x1, -1);
                int count_y = std::min(y2, Height() - 1) - std::max(y1, -1);
                res.at(y, x)[0] = (v22 - v12 - v21 + v11) / (count_x * count_y);
            }
    }
    return res;
}

template <typename D>
Image<D> Image<D>::GaussianBlur(int ksize, float sigmaX) const {
    if (ksize <= 1)
        return Clone();
    if ((ksize & 1) == 0)
        ++ksize;  // enforce odd

    if (sigmaX <= 0.f) {
        // OpenCV-compatible heuristic for sigma from kernel size
        float half = 0.5f * (ksize - 1);
        sigmaX = 0.3f * (half - 1.f) + 0.8f;
        if (sigmaX <= 0.f)
            sigmaX = 0.8f;
    }

    const int radius = ksize / 2;

    // Build 1D Gaussian kernel
    std::vector<float> kernel(ksize);
    const float inv2Sigma2 = 1.0f / (2.0f * sigmaX * sigmaX);
    float sumw = 0.f;
    for (int i = -radius; i <= radius; ++i) {
        float w = std::exp(-(i * i) * inv2Sigma2);
        kernel[i + radius] = w;
        sumw += w;
    }
    const float invSum = 1.0f / sumw;
    for (int i = 0; i < ksize; ++i) kernel[i] *= invSum;

    // Temporary buffer for horizontal pass (float for precision)
    std::vector<float> tmp(static_cast<size_t>(width_) * height_ * channels_);

    // Precompute clamped index tables to remove inner clamps
    std::vector<int> horizIndices(static_cast<size_t>(width_) * ksize);
    for (int x = 0; x < width_; ++x) {
        for (int k = -radius; k <= radius; ++k) {
            int idx = x + k;
            if (idx < 0) idx = 0; else if (idx >= width_) idx = width_ - 1;
            horizIndices[static_cast<size_t>(x) * ksize + (k + radius)] = idx;
        }
    }
    std::vector<int> vertIndices(static_cast<size_t>(height_) * ksize);
    for (int y = 0; y < height_; ++y) {
        for (int k = -radius; k <= radius; ++k) {
            int idy = y + k;
            if (idy < 0) idy = 0; else if (idy >= height_) idy = height_ - 1;
            vertIndices[static_cast<size_t>(y) * ksize + (k + radius)] = idy;
        }
    }

    // Parallel setup
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    unsigned int numWorkers = std::min<unsigned int>(hw, static_cast<unsigned int>(height_));
    auto parallelForRows = [&](int rows, const std::function<void(int,int)> &fn) {
        if (numWorkers <= 1 || rows < 2) {
            fn(0, rows);
            return;
        }
        std::vector<std::thread> workers;
        workers.reserve(numWorkers);
        int chunk = (rows + static_cast<int>(numWorkers) - 1) / static_cast<int>(numWorkers);
        int start = 0;
        for (unsigned int t = 0; t < numWorkers && start < rows; ++t) {
            int end = std::min(start + chunk, rows);
            workers.emplace_back([=,&fn]() { fn(start, end); });
            start = end;
        }
        for (auto &th : workers) th.join();
    };

    // Horizontal pass (row-parallel)
    parallelForRows(height_, [&](int yBegin, int yEnd) {
        for (int y = yBegin; y < yEnd; ++y) {
            const D* rowPtr = Row(y);
            for (int x = 0; x < width_; ++x) {
                const int* xIdx = &horizIndices[static_cast<size_t>(x) * ksize];
                size_t base = (static_cast<size_t>(y) * width_ + x) * channels_;
                for (int c = 0; c < channels_; ++c) {
                    float acc = 0.f;
                    for (int k = 0; k < ksize; ++k) {
                        int xx = xIdx[k];
                        acc += static_cast<float>(rowPtr[xx * channels_ + c]) * kernel[k];
                    }
                    tmp[base + c] = acc;
                }
            }
        }
    });

    // Vertical pass -> output (row-parallel)
    Image<D> dst;
    dst.Reset(width_, height_, channels_);
    D* dst_data = dst.Data();

    // Precompute numeric limits for saturation
    const float lo = static_cast<float>(std::numeric_limits<D>::lowest());
    const float hi = static_cast<float>(std::numeric_limits<D>::max());

    parallelForRows(height_, [&](int yBegin, int yEnd) {
        for (int y = yBegin; y < yEnd; ++y) {
            const int* yIdx = &vertIndices[static_cast<size_t>(y) * ksize];
            if (channels_ == 1) {
                int x = 0;
#if defined(__AVX2__)
                for (; x + 8 <= width_; x += 8) {
                    __m256 acc0 = _mm256_setzero_ps();
                    for (int k = 0; k < ksize; ++k) {
                        int yy = yIdx[k];
                        const float* src = &tmp[static_cast<size_t>(yy) * width_ + x];
                        __m256 s = _mm256_loadu_ps(src);
                        __m256 w = _mm256_set1_ps(kernel[k]);
#  if defined(__FMA__)
                        acc0 = _mm256_fmadd_ps(s, w, acc0);
#  else
                        acc0 = _mm256_add_ps(acc0, _mm256_mul_ps(s, w));
#  endif
                    }
                    alignas(32) float buf[8];
                    _mm256_storeu_ps(buf, acc0);
                    size_t baseOut = static_cast<size_t>(y) * width_ + x;
                    for (int i = 0; i < 8; ++i) {
                        float v = buf[i];
                        if (std::is_integral<D>::value) v = std::floor(v + 0.5f);
                        v = v < lo ? lo : (v > hi ? hi : v);
                        dst_data[baseOut + i] = static_cast<D>(v);
                    }
                }
#elif defined(__SSE2__)
                for (; x + 4 <= width_; x += 4) {
                    __m128 acc0 = _mm_setzero_ps();
                    for (int k = 0; k < ksize; ++k) {
                        int yy = yIdx[k];
                        const float* src = &tmp[static_cast<size_t>(yy) * width_ + x];
                        __m128 s = _mm_loadu_ps(src);
                        __m128 w = _mm_set1_ps(kernel[k]);
                        acc0 = _mm_add_ps(acc0, _mm_mul_ps(s, w));
                    }
                    alignas(16) float buf[4];
                    _mm_storeu_ps(buf, acc0);
                    size_t baseOut = static_cast<size_t>(y) * width_ + x;
                    for (int i = 0; i < 4; ++i) {
                        float v = buf[i];
                        if (std::is_integral<D>::value) v = std::floor(v + 0.5f);
                        v = v < lo ? lo : (v > hi ? hi : v);
                        dst_data[baseOut + i] = static_cast<D>(v);
                    }
                }
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__))
                for (; x + 4 <= width_; x += 4) {
                    float32x4_t acc0 = vdupq_n_f32(0.0f);
                    for (int k = 0; k < ksize; ++k) {
                        int yy = yIdx[k];
                        const float* src = &tmp[static_cast<size_t>(yy) * width_ + x];
                        float32x4_t s = vld1q_f32(src);
                        float32x4_t w = vdupq_n_f32(kernel[k]);
                        acc0 = vmlaq_f32(acc0, s, w);
                    }
                    alignas(16) float buf[4];
                    vst1q_f32(buf, acc0);
                    size_t baseOut = static_cast<size_t>(y) * width_ + x;
                    for (int i = 0; i < 4; ++i) {
                        float v = buf[i];
                        if (std::is_integral<D>::value) v = std::floor(v + 0.5f);
                        v = v < lo ? lo : (v > hi ? hi : v);
                        dst_data[baseOut + i] = static_cast<D>(v);
                    }
                }
#endif
                // scalar tail
                for (; x < width_; ++x) {
                    float acc = 0.f;
                    for (int k = 0; k < ksize; ++k) {
                        int yy = yIdx[k];
                        acc += tmp[static_cast<size_t>(yy) * width_ + x] * kernel[k];
                    }
                    if (std::is_integral<D>::value) acc = std::floor(acc + 0.5f);
                    acc = acc < lo ? lo : (acc > hi ? hi : acc);
                    dst_data[static_cast<size_t>(y) * width_ + x] = static_cast<D>(acc);
                }
            } else {
                // Fallback: original scalar path for multi-channel
                for (int x = 0; x < width_; ++x) {
                    size_t baseOut = (static_cast<size_t>(y) * width_ + x) * channels_;
                    for (int c = 0; c < channels_; ++c) {
                        float acc = 0.f;
                        for (int k = 0; k < ksize; ++k) {
                            int yy = yIdx[k];
                            size_t baseIn = (static_cast<size_t>(yy) * width_ + x) * channels_ + c;
                            acc += tmp[baseIn] * kernel[k];
                        }
                        if (std::is_integral<D>::value) acc = std::floor(acc + 0.5f);
                        acc = acc < lo ? lo : (acc > hi ? hi : acc);
                        dst_data[baseOut + c] = static_cast<D>(acc);
                    }
                }
            }
        }
    });

    return dst;
}

template <typename D>
Image<D> Image<D>::MinFilter(int kernel_left, int kernel_right, int kernel_top,
                             int kernel_bottom) const {
    INSPIRECV_CHECK(Channels() == 1) << "channels=" << Channels();
    const int W = Width();
    const int H = Height();
    const int L = kernel_left + 1;
    const int R = kernel_right + 1;
    const int T = kernel_top + 1;
    const int B = kernel_bottom + 1;

    Image<D> tmp_image;
    tmp_image.Reset(W, H, 1);

    // Horizontal pass using trailing minima (left) and leading minima (right via reversed trailing)
    {
        std::vector<D> left_trailing(static_cast<size_t>(W));
        std::vector<D> right_leading(static_cast<size_t>(W));

        auto trailing_min = [&](const D* row, D* out, int n, int win) {
            std::deque<int> dq;
            for (int i = 0; i < n; ++i) {
                while (!dq.empty() && row[dq.back()] >= row[i]) dq.pop_back();
                dq.push_back(i);
                int head = i - win + 1;  // head of window
                if (dq.front() < head) dq.pop_front();
                out[i] = row[dq.front()];
            }
        };

        auto leading_min = [&](const D* row, D* out, int n, int win) {
            // compute trailing minima on reversed row, then map back
            std::deque<int> dq;
            for (int i = n - 1; i >= 0; --i) {
                int ri = n - 1 - i;  // reversed index increasing
                D v = row[i];
                while (!dq.empty() && row[n - 1 - dq.back()] >= v) dq.pop_back();
                dq.push_back(ri);
                int head = ri - win + 1;
                if (dq.front() < head) dq.pop_front();
                out[i] = row[n - 1 - dq.front()];
            }
        };

        for (int y = 0; y < H; ++y) {
            const D* row = Row(y);
            D* tmp_row = tmp_image.Row(y);

            if (kernel_left == 0 && kernel_right == 0) {
                std::memcpy(tmp_row, row, static_cast<size_t>(W) * sizeof(D));
                continue;
            }

            trailing_min(row, left_trailing.data(), W, L);
            leading_min(row, right_leading.data(), W, R);

            // Combine per-position minima
            int x = 0;
#if defined(__AVX2__)
            if (std::is_same<D, uint8_t>::value) {
                for (; x + 32 <= W; x += 32) {
                    __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(left_trailing.data() + x));
                    __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(right_leading.data() + x));
                    __m256i m = _mm256_min_epu8(a, b);
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(tmp_row + x), m);
                }
            }
#endif
#if defined(__SSE2__)
            if (std::is_same<D, uint8_t>::value) {
                for (; x + 16 <= W; x += 16) {
                    __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(left_trailing.data() + x));
                    __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(right_leading.data() + x));
                    __m128i m = _mm_min_epu8(a, b);
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(tmp_row + x), m);
                }
            }
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
            if (std::is_same<D, uint8_t>::value) {
                for (; x + 16 <= W; x += 16) {
                    uint8x16_t a = vld1q_u8(reinterpret_cast<const uint8_t*>(left_trailing.data() + x));
                    uint8x16_t b = vld1q_u8(reinterpret_cast<const uint8_t*>(right_leading.data() + x));
                    uint8x16_t m = vminq_u8(a, b);
                    vst1q_u8(reinterpret_cast<uint8_t*>(tmp_row + x), m);
                }
            }
#endif
            for (; x < W; ++x) tmp_row[x] = std::min(left_trailing[x], right_leading[x]);
        }
    }

    // Vertical pass using column-wise trailing/leading minima
    if (kernel_top == 0 && kernel_bottom == 0) {
        return tmp_image;
    }

    Image<D> res_image;
    res_image.Reset(W, H, 1);

    std::vector<D> col(static_cast<size_t>(H));
    std::vector<D> top_trailing(static_cast<size_t>(H));
    std::vector<D> bottom_leading(static_cast<size_t>(H));

    auto trailing_min_col = [&](const D* c, D* out, int n, int win) {
        std::deque<int> dq;
        for (int i = 0; i < n; ++i) {
            while (!dq.empty() && c[dq.back()] >= c[i]) dq.pop_back();
            dq.push_back(i);
            int head = i - win + 1;
            if (dq.front() < head) dq.pop_front();
            out[i] = c[dq.front()];
        }
    };

    auto leading_min_col = [&](const D* c, D* out, int n, int win) {
        std::deque<int> dq;
        for (int i = n - 1; i >= 0; --i) {
            int ri = n - 1 - i;
            D v = c[i];
            while (!dq.empty() && c[n - 1 - dq.back()] >= v) dq.pop_back();
            dq.push_back(ri);
            int head = ri - win + 1;
            if (dq.front() < head) dq.pop_front();
            out[i] = c[n - 1 - dq.front()];
        }
    };

    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) col[y] = tmp_image.Row(y)[x];
        trailing_min_col(col.data(), top_trailing.data(), H, T);
        leading_min_col(col.data(), bottom_leading.data(), H, B);
        for (int y = 0; y < H; ++y) res_image.Row(y)[x] = std::min(top_trailing[y], bottom_leading[y]);
    }

    return res_image;
}

template <typename D>
Image<D> Image<D>::MaxFilter(int kernel_left, int kernel_right, int kernel_top,
                             int kernel_bottom) const {
    INSPIRECV_CHECK(Channels() == 1) << "channels=" << Channels();
    const int W = Width();
    const int H = Height();
    const int L = kernel_left + 1;
    const int R = kernel_right + 1;
    const int T = kernel_top + 1;
    const int B = kernel_bottom + 1;

    Image<D> tmp_image;
    tmp_image.Reset(W, H, 1);

    // Horizontal pass using trailing maxima (left) and leading maxima (right via reversed trailing)
    {
        std::vector<D> left_trailing(static_cast<size_t>(W));
        std::vector<D> right_leading(static_cast<size_t>(W));

        auto trailing_max = [&](const D* row, D* out, int n, int win) {
            std::deque<int> dq;
            for (int i = 0; i < n; ++i) {
                while (!dq.empty() && row[dq.back()] <= row[i]) dq.pop_back();
                dq.push_back(i);
                int head = i - win + 1;
                if (dq.front() < head) dq.pop_front();
                out[i] = row[dq.front()];
            }
        };

        auto leading_max = [&](const D* row, D* out, int n, int win) {
            std::deque<int> dq;
            for (int i = n - 1; i >= 0; --i) {
                int ri = n - 1 - i;
                D v = row[i];
                while (!dq.empty() && row[n - 1 - dq.back()] <= v) dq.pop_back();
                dq.push_back(ri);
                int head = ri - win + 1;
                if (dq.front() < head) dq.pop_front();
                out[i] = row[n - 1 - dq.front()];
            }
        };

        for (int y = 0; y < H; ++y) {
            const D* row = Row(y);
            D* tmp_row = tmp_image.Row(y);

            if (kernel_left == 0 && kernel_right == 0) {
                std::memcpy(tmp_row, row, static_cast<size_t>(W) * sizeof(D));
                continue;
            }

            trailing_max(row, left_trailing.data(), W, L);
            leading_max(row, right_leading.data(), W, R);

            int x = 0;
#if defined(__AVX2__)
            if (std::is_same<D, uint8_t>::value) {
                for (; x + 32 <= W; x += 32) {
                    __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(left_trailing.data() + x));
                    __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(right_leading.data() + x));
                    __m256i m = _mm256_max_epu8(a, b);
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(tmp_row + x), m);
                }
            }
#endif
#if defined(__SSE2__)
            if (std::is_same<D, uint8_t>::value) {
                for (; x + 16 <= W; x += 16) {
                    __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(left_trailing.data() + x));
                    __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(right_leading.data() + x));
                    __m128i m = _mm_max_epu8(a, b);
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(tmp_row + x), m);
                }
            }
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
            if (std::is_same<D, uint8_t>::value) {
                for (; x + 16 <= W; x += 16) {
                    uint8x16_t a = vld1q_u8(reinterpret_cast<const uint8_t*>(left_trailing.data() + x));
                    uint8x16_t b = vld1q_u8(reinterpret_cast<const uint8_t*>(right_leading.data() + x));
                    uint8x16_t m = vmaxq_u8(a, b);
                    vst1q_u8(reinterpret_cast<uint8_t*>(tmp_row + x), m);
                }
            }
#endif
            for (; x < W; ++x) tmp_row[x] = std::max(left_trailing[x], right_leading[x]);
        }
    }

    if (kernel_top == 0 && kernel_bottom == 0) {
        return tmp_image;
    }

    Image<D> res_image;
    res_image.Reset(W, H, 1);

    std::vector<D> col(static_cast<size_t>(H));
    std::vector<D> top_trailing(static_cast<size_t>(H));
    std::vector<D> bottom_leading(static_cast<size_t>(H));

    auto trailing_max_col = [&](const D* c, D* out, int n, int win) {
        std::deque<int> dq;
        for (int i = 0; i < n; ++i) {
            while (!dq.empty() && c[dq.back()] <= c[i]) dq.pop_back();
            dq.push_back(i);
            int head = i - win + 1;
            if (dq.front() < head) dq.pop_front();
            out[i] = c[dq.front()];
        }
    };

    auto leading_max_col = [&](const D* c, D* out, int n, int win) {
        std::deque<int> dq;
        for (int i = n - 1; i >= 0; --i) {
            int ri = n - 1 - i;
            D v = c[i];
            while (!dq.empty() && c[n - 1 - dq.back()] <= v) dq.pop_back();
            dq.push_back(ri);
            int head = ri - win + 1;
            if (dq.front() < head) dq.pop_front();
            out[i] = c[n - 1 - dq.front()];
        }
    };

    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) col[y] = tmp_image.Row(y)[x];
        trailing_max_col(col.data(), top_trailing.data(), H, T);
        leading_max_col(col.data(), bottom_leading.data(), H, B);
        for (int y = 0; y < H; ++y) res_image.Row(y)[x] = std::max(top_trailing[y], bottom_leading[y]);
    }

    return res_image;
}

template <typename D>
Image<D> Image<D>::FlipLeftRight() const {
    Image<D> dst;
    dst.Reset(width_, height_, channels_);
    for (int i = 0; i < height_; ++i)
        for (int j = 0; j < width_; ++j) {
            memcpy(dst.at(i, j), at(i, width_ - j - 1), sizeof(D) * channels_);
        }
    return dst;
}

template <typename D>
Image<D> Image<D>::FlipUpDown() const {
    Image<D> dst;
    dst.Reset(width_, height_, channels_);
    for (int i = 0; i < height_; ++i) {
        memcpy(dst.Row(i), Row(height_ - i - 1), sizeof(D) * width_ * channels_);
    }
    return dst;
}

template <typename D>
Image<D> Image<D>::FlipChannels() const {
    Image<D> dst;
    dst.Reset(width_, height_, channels_);
    auto src_iter = Data();
    auto dst_iter = dst.Data();
    for (int i = 0; i < height_ * width_; ++i) {
        for (int c = 0; c < channels_; ++c) {
            *(dst_iter++) = src_iter[channels_ - c - 1];
        }
        src_iter += channels_;
    }
    return dst;
}

template <typename D>
Image<D> Image<D>::Rotate180() const {
    Image<D> dst;
    dst.Reset(width_, height_, channels_);
    for (int i = 0; i < height_; ++i)
        for (int j = 0; j < width_; ++j) {
            memcpy(dst.at(i, j), at(height_ - i - 1, width_ - j - 1), sizeof(D) * channels_);
        }
    return dst;
}

template <typename D>
Image<D> Image<D>::Rotate90() const {
    Image<D> dst;
    dst.Reset(height_, width_, channels_);
    for (int i = 0; i < dst.height_; ++i) {
        for (int j = 0; j < dst.width_; ++j) {
            memcpy(dst.at(i, j), at(height_ - j - 1, i), sizeof(D) * channels_);
        }
    }
    return dst;
}

template <typename D>
Image<D> Image<D>::Rotate270() const {
    Image<D> dst;
    dst.Reset(height_, width_, channels_);
    for (int i = 0; i < dst.height_; ++i) {
        for (int j = 0; j < dst.width_; ++j) {
            memcpy(dst.at(i, j), at(j, width_ - i - 1), sizeof(D) * channels_);
        }
    }
    return dst;
}

template <typename D>
Image<D> Image<D>::RgbToGray() const {
    Image<D> dst;
    INSPIRECV_CHECK_EQ(channels_, 3);
    dst.Reset(width_, height_, 1);
    for (int i = 0; i < height_; ++i)
        for (int j = 0; j < width_; ++j) {
            auto p = this->at(i, j);
            dst.at(i, j)[0] = p[0] * 0.299 + p[1] * 0.587 + p[2] * 0.114;
        }
    return dst;
}

template <typename D>
Image<D> Image<D>::SwapRB() const {
    INSPIRECV_CHECK_EQ(channels_, 3);
    Image<D> dst;
    dst.Reset(width_, height_, channels_);
    auto src_iter = Data();
    auto dst_iter = dst.Data();
    for (int i = 0; i < height_ * width_; ++i) {
        dst_iter[0] = src_iter[2];
        dst_iter[1] = src_iter[1];
        dst_iter[2] = src_iter[0];
        src_iter += channels_;
        dst_iter += channels_;
    }
    return dst;
}

template <typename D>
Rect2i Image<D>::GetMaskRect(D threshold) const {
    INSPIRECV_CHECK(!Empty());

    int left = Width() - 1;
    int right = 0;
    int top = Height() - 1;
    int bottom = 0;

    auto iter = Data();
    for (int i = 0; i < Height(); ++i)
        for (int j = 0; j < Width(); ++j) {
            if (*iter > threshold) {
                left = std::min(left, j);
                right = std::max(right, j);
                top = std::min(top, i);
                bottom = std::max(bottom, i);
            }
            ++iter;
        }

    return Rect2i(left, top, right, bottom);
}

template <typename D>
Status Image<D>::FillRect(const Rect2i &rect, const std::vector<D> &color) {
    if (color.size() != channels_) {
        std::stringstream msg;
        msg << "color.size()=" << color.size() << ", channels_=" << channels_;
        return Status(error::INVALID_ARGUMENT, msg.str());
    }
    auto xmin = std::max(0, rect.xmin());
    auto ymin = std::max(0, rect.ymin());
    auto xmax = std::min(width_, rect.xmax());
    auto ymax = std::min(height_, rect.ymax());
    for (int i = ymin; i < ymax; ++i) {
        auto iter = at(i, xmin);
        for (int j = xmin; j < xmax; ++j) {
            memcpy(iter, color.data(), channels_ * sizeof(D));
            iter += channels_;
        }
    }
    return Status::OK();
}

template <typename D>
Status Image<D>::FillCircle(const Point2f &center, float radius, const std::vector<D> &color) {
    int top = std::ceil(std::max(0.f, center.y - radius));
    int bottom = std::min(height_ - 1.f, center.y + radius);
    for (int y = top; y <= bottom; ++y) {
        double d = std::sqrt(radius * radius - (y - center.y) * (y - center.y));
        int left = std::max(static_cast<int>(std::ceil(center.x - d)), 0);
        int right = std::min(static_cast<int>(center.x + d), width_ - 1);
        auto iter = at(y, left);
        for (int x = left; x <= right; ++x) {
            memcpy(iter, color.data(), channels_ * sizeof(D));
            iter += channels_;
        }
    }
    return Status::OK();
}

template <typename D>
Status Image<D>::DrawPoint(const Point2f &point, float thinkness, const std::vector<D> &color) {
    INSPIRECV_RETURN_IF_ERROR(FillCircle(point, thinkness, color));
    return Status::OK();
}

template <typename D>
Status Image<D>::DrawLine(const Point2i &p0, const Point2i &p1, const std::vector<D> &color,
                          int thickness) {
    if (p0 == p1) {
        return Status(error::INVALID_ARGUMENT, "Same points!");
    }
    int b0 = thickness / 2;
    int b1 = thickness - b0;
    if (p0.x == p1.x) {
        Rect2i rect(p0.x - b0, std::min(p0.y, p1.y), p0.x + b1, std::max(p0.y, p1.y));
        INSPIRECV_RETURN_IF_ERROR(FillRect(rect, color));
    } else if (p0.y == p1.y) {
        Rect2i rect(std::min(p0.x, p1.x), p0.y - b0, std::max(p0.x, p1.x), p0.y + b1);
        INSPIRECV_RETURN_IF_ERROR(FillRect(rect, color));
    } else {
        double k = static_cast<double>(p1.y - p0.y) / (p1.x - p0.x);
        double b = p0.y - p0.x * k;
        if (-1 <= k && k <= 1) {
            auto xmin = std::max(std::min(p0.x, p1.x), 0);
            auto xmax = std::min(std::max(p0.x, p1.x), width_ - 1);
            for (int x = xmin; x <= xmax; ++x) {
                int y_c = static_cast<int>(k * x + b + 0.5);
                int ymin = std::max(0, y_c - b0);
                int ymax = std::min(height_ - 1, y_c + b1 - 1);
                for (int y = ymin; y <= ymax; ++y)
                    memcpy(at(y, x), color.data(), channels_ * sizeof(D));
            }
        } else {
            auto ymin = std::max(std::min(p0.y, p1.y), 0);
            auto ymax = std::min(std::max(p0.y, p1.y), height_ - 1);
            for (int y = ymin; y <= ymax; ++y) {
                int x_c = static_cast<int>((y - b) / k + 0.5);
                int xmin = std::max(0, x_c - b0);
                int xmax = std::min(width_ - 1, x_c + b1 - 1);
                for (int x = xmin; x <= xmax; ++x)
                    memcpy(at(y, x), color.data(), channels_ * sizeof(D));
            }
        }
    }
    return Status::OK();
}

template <typename D>
Status Image<D>::DrawRect(const Rect2i &rect, const std::vector<D> &color, int thickness) {
    INSPIRECV_RETURN_IF_ERROR(DrawLine(Point2i(rect.xmin(), rect.ymin()),
                                       Point2i(rect.xmin(), rect.ymax()), color, thickness));
    INSPIRECV_RETURN_IF_ERROR(DrawLine(Point2i(rect.xmin(), rect.ymax()),
                                       Point2i(rect.xmax(), rect.ymax()), color, thickness));
    INSPIRECV_RETURN_IF_ERROR(DrawLine(Point2i(rect.xmax(), rect.ymax()),
                                       Point2i(rect.xmax(), rect.ymin()), color, thickness));
    INSPIRECV_RETURN_IF_ERROR(DrawLine(Point2i(rect.xmax(), rect.ymin()),
                                       Point2i(rect.xmin(), rect.ymin()), color, thickness));
    return Status::OK();
}

template <typename D>
Rect2i Image<D>::GetSafeRect(const Rect2i &rect) const {
    return Rect2i(std::max(0, rect.xmin()), std::max(0, rect.ymin()),
                  std::min(width_ - 1, rect.xmax()), std::min(height_ - 1, rect.ymax()));
}

#ifdef INSPIRECV_BACKEND_OKCV_USE_OPENCV

template <typename dtype>
void Image<dtype>::FromCVMat(const cv::Mat &mat, bool swap_channels) {
    Reset(mat.size().width, mat.size().height, mat.channels());
    auto data_ptr = Data();
    auto depth_type = (mat.type() & CV_MAT_DEPTH_MASK);
    INSPIRECV_CHECK(depth_type == CV_8U || depth_type == CV_32F);
    if (depth_type == CV_8U) {
        for (int i = 0; i < height_; ++i) {
            for (int j = 0; j < width_; ++j) {
                if (channels_ == 3) {
                    cv::Vec3b intensity = mat.at<cv::Vec3b>(i, j);
                    if (swap_channels) {
                        *data_ptr++ = static_cast<dtype>(intensity.val[2]);
                        *data_ptr++ = static_cast<dtype>(intensity.val[1]);
                        *data_ptr++ = static_cast<dtype>(intensity.val[0]);
                    } else {
                        *data_ptr++ = static_cast<dtype>(intensity.val[0]);
                        *data_ptr++ = static_cast<dtype>(intensity.val[1]);
                        *data_ptr++ = static_cast<dtype>(intensity.val[2]);
                    }
                } else {
                    *data_ptr++ = static_cast<dtype>(mat.at<uchar>(i, j));
                }
            }
        }
    } else {
        for (int i = 0; i < height_; ++i) {
            for (int j = 0; j < width_; ++j) {
                if (channels_ == 3) {
                    cv::Vec3f intensity = mat.at<cv::Vec3f>(i, j);
                    if (swap_channels) {
                        *data_ptr++ = static_cast<dtype>(intensity.val[2]);
                        *data_ptr++ = static_cast<dtype>(intensity.val[1]);
                        *data_ptr++ = static_cast<dtype>(intensity.val[0]);
                    } else {
                        *data_ptr++ = static_cast<dtype>(intensity.val[0]);
                        *data_ptr++ = static_cast<dtype>(intensity.val[1]);
                        *data_ptr++ = static_cast<dtype>(intensity.val[2]);
                    }
                } else {
                    *data_ptr++ = static_cast<dtype>(mat.at<float>(i, j));
                }
            }
        }
    }
}

template <>
void Image<float>::ToCVMat(cv::Mat &mat, bool swap_channels) const {
    INSPIRECV_CHECK(channels_ == 1 || channels_ == 3 || channels_ == 4)
      << "channels_ = " << channels_;
    int flag = channels_ == 1 ? CV_32FC1 : CV_32FC3;
    if (channels_ == 4)
        flag = CV_32FC4;
    mat = cv::Mat(height_, width_, flag);
    auto data_ptr = Data();
    float *mat_data_ptr = reinterpret_cast<float *>(mat.data);
    for (int i = 0; i < height_ * width_; ++i) {
        if (channels_ == 3) {
            if (swap_channels) {
                *mat_data_ptr++ = data_ptr[2];
                *mat_data_ptr++ = data_ptr[1];
                *mat_data_ptr++ = data_ptr[0];
            } else {
                *mat_data_ptr++ = data_ptr[0];
                *mat_data_ptr++ = data_ptr[1];
                *mat_data_ptr++ = data_ptr[2];
            }
            data_ptr += 3;
        } else if (channels_ == 1) {
            *mat_data_ptr++ = *data_ptr++;
        } else {
            if (swap_channels) {
                *mat_data_ptr++ = data_ptr[2];
                *mat_data_ptr++ = data_ptr[1];
                *mat_data_ptr++ = data_ptr[0];
            } else {
                *mat_data_ptr++ = data_ptr[0];
                *mat_data_ptr++ = data_ptr[1];
                *mat_data_ptr++ = data_ptr[2];
            }
            *mat_data_ptr++ = data_ptr[3];
            data_ptr += 4;
        }
    }
}
template <>
void Image<uint8_t>::ToCVMat(cv::Mat &mat, bool swap_channels) const {
    INSPIRECV_CHECK(channels_ == 1 || channels_ == 3 || channels_ == 4)
      << "channels_ = " << channels_;
    int flag = channels_ == 1 ? CV_8UC1 : CV_8UC3;
    if (channels_ == 4)
        flag = CV_8UC4;
    mat = cv::Mat(height_, width_, flag);
    auto data_ptr = Data();
    auto mat_data_ptr = mat.data;
    for (int i = 0; i < height_ * width_; ++i) {
        if (channels_ == 3) {
            if (swap_channels) {
                *mat_data_ptr++ = data_ptr[2];
                *mat_data_ptr++ = data_ptr[1];
                *mat_data_ptr++ = data_ptr[0];
            } else {
                *mat_data_ptr++ = data_ptr[0];
                *mat_data_ptr++ = data_ptr[1];
                *mat_data_ptr++ = data_ptr[2];
            }
            data_ptr += 3;
        } else if (channels_ == 1) {
            *mat_data_ptr++ = *data_ptr++;
        } else {
            if (swap_channels) {
                *mat_data_ptr++ = data_ptr[2];
                *mat_data_ptr++ = data_ptr[1];
                *mat_data_ptr++ = data_ptr[0];
            } else {
                *mat_data_ptr++ = data_ptr[0];
                *mat_data_ptr++ = data_ptr[1];
                *mat_data_ptr++ = data_ptr[2];
            }
            *mat_data_ptr++ = data_ptr[3];
            data_ptr += 4;
        }
    }
}

#endif  // INSPIRECV_BACKEND_OKCV_USE_OPENCV

template class Image<uint8_t>;
template class Image<float>;

}  // namespace okcv