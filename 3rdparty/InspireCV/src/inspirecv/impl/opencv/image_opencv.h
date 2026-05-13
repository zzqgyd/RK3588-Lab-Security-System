#ifndef INSPIRECV_IMPL_OPENCV_IMAGE_OPENCV_H
#define INSPIRECV_IMPL_OPENCV_IMAGE_OPENCV_H

#include "opencv2/opencv.hpp"
#include "inspirecv/core/image.h"
#include "inspirecv/core/rect.h"
#include "inspirecv/core/point.h"
#include "inspirecv/core/transform_matrix.h"

namespace inspirecv {

class Image::Impl {
public:
    // Creation and conversion
    Impl(const cv::Mat& mat) : image_(std::move(mat)) {}

    // copy_data is ignored
    Impl(int width, int height, int channels, const uint8_t* data = nullptr,
         bool copy_data = true) {
        image_.create(height, width, CV_8UC(channels));
        if (data) {
            std::memcpy(image_.data, data, width * height * channels);
        }
    }

    // copy_data is ignored
    void Reset(int width, int height, int channels, const uint8_t* data = nullptr,
               bool copy_data = true) {
        image_.create(height, width, CV_8UC(channels));
        if (data) {
            std::memcpy(image_.data, data, width * height * channels);
        }
    }

    Image Clone() const {
        Image result;
        result.impl_->image_ = image_.clone();
        return result;
    }

    // Basic properties
    ~Impl() = default;

    // Basic properties
    int Width() const {
        return image_.cols;
    }
    int Height() const {
        return image_.rows;
    }
    int Channels() const {
        return image_.channels();
    }
    bool Empty() const {
        return image_.empty();
    }
    const uint8_t* Data() const {
        return image_.data;
    }

    // Get internal image implementation
    void* GetInternalImage() const {
        return image_.data;
    }

    // I/O operations
    bool Read(const std::string& filename, int channels) {
        int flag = channels == 3 ? cv::IMREAD_COLOR : cv::IMREAD_GRAYSCALE;
        image_ = cv::imread(filename, flag);
        return !image_.empty();
    }

    bool Write(const std::string& filename) const {
        return cv::imwrite(filename, image_);
    }

    void Show(const std::string& window_name, int delay) const {
        cv::imshow(window_name, image_);
        cv::waitKey(delay);
    }

    // Basic operations
    void Fill(double value) {
        image_.setTo(cv::Scalar::all(value));
    }

    Image Mul(double scale) const {
        Image result;
        image_.convertTo(result.impl_->image_, -1, scale);
        return result;
    }

    Image Add(double value) const {
        Image result;
        cv::Scalar scalar;
        if (image_.channels() == 3) {
            scalar = cv::Scalar(value, value, value);
        } else {
            scalar = cv::Scalar(value);
        }
        cv::add(image_, scalar, result.impl_->image_);
        return result;
    }

    Image Resize(int width, int height, bool use_linear = true) const {
        Image result;
        cv::resize(image_, result.impl_->image_, cv::Size(width, height), 0, 0,
                   use_linear ? cv::INTER_LINEAR : cv::INTER_NEAREST);
        return result;
    }

    Image Crop(const Rect<int>& rect) const {
        Image result;
        cv::Rect roi(rect.GetX(), rect.GetY(), rect.GetWidth(), rect.GetHeight());
        result.impl_->image_ = image_(roi).clone();
        return result;
    }

    Image WarpAffine(const TransformMatrix& matrix, int width, int height) const {
        Image result;
        const cv::Mat& M = *static_cast<const cv::Mat*>(matrix.GetInternalMatrix());
        cv::Mat inv_mat;
        // Because opencv's warpaffine requires the inverse of the transform matrix
        cv::invertAffineTransform(M.rowRange(0, 2),
                                  inv_mat);  // Get first 2 rows for affine transform
        cv::warpAffine(image_, result.impl_->image_, inv_mat, cv::Size(width, height));
        return result;
    }

    Image Rotate90() const {
        Image result;
        cv::Mat result_mat;
        cv::rotate(image_, result_mat, cv::ROTATE_90_CLOCKWISE);
        result.impl_->image_ = result_mat;
        return result;
    }

    Image Rotate180() const {
        Image result;
        cv::Mat result_mat;
        cv::rotate(image_, result_mat, cv::ROTATE_180);
        result.impl_->image_ = result_mat;
        return result;
    }

    Image Rotate270() const {
        Image result;
        cv::Mat result_mat;
        cv::rotate(image_, result_mat, cv::ROTATE_90_COUNTERCLOCKWISE);
        result.impl_->image_ = result_mat;
        return result;
    }

    Image SwapRB() const {
        Image result;
        cv::cvtColor(image_, result.impl_->image_, cv::COLOR_BGR2RGB);
        return result;
    }

    Image FlipHorizontal() const {
        Image result;
        cv::Mat result_mat;
        cv::flip(image_, result_mat, 1);
        result.impl_->image_ = result_mat;
        return result;
    }

    Image FlipVertical() const {
        Image result;
        cv::Mat result_mat;
        cv::flip(image_, result_mat, 0);
        result.impl_->image_ = result_mat;
        return result;
    }

    Image Pad(int top, int bottom, int left, int right, const std::vector<double>& color) const {
        Image result;
        cv::Scalar cv_color;
        if (color.empty()) {
            cv_color = cv::Scalar(0);
        } else if (image_.channels() == 1) {
            cv_color = cv::Scalar(color[0]);
        } else if (image_.channels() == 3) {
            // Convert RGB to BGR for OpenCV
            cv_color = cv::Scalar(color[2], color.size() > 1 ? color[1] : 0,
                                  color.size() > 2 ? color[0] : 0);
        } else if (image_.channels() == 4) {
            // Convert RGBA to BGRA for OpenCV
            cv_color = cv::Scalar(color[2], color.size() > 1 ? color[1] : 0,
                                  color.size() > 2 ? color[0] : 0, color.size() > 3 ? color[3] : 0);
        }
        cv::copyMakeBorder(image_, result.impl_->image_, top, bottom, left, right,
                           cv::BORDER_CONSTANT, cv_color);
        return result;
    }

    // Image format conversion
    Image ToGray() const {
        Image result;
        cv::cvtColor(image_, result.impl_->image_, cv::COLOR_BGR2GRAY);
        return result;
    }

    // Drawing operations
    void DrawLine(const Point<int>& p1, const Point<int>& p2, const std::vector<double>& color,
                  int thickness = 1) {
        cv::Scalar cv_color;
        if (!color.empty()) {
            if (image_.channels() >= 3) {
                // Convert RGB to BGR for OpenCV
                cv_color = cv::Scalar(color[2], color.size() > 1 ? color[1] : 0,
                                      color.size() > 2 ? color[0] : 0);
            } else {
                cv_color = cv::Scalar(color[0]);
            }
        }
        cv::Point cv_p1 = *static_cast<cv::Point*>(p1.GetInternalPoint());
        cv::Point cv_p2 = *static_cast<cv::Point*>(p2.GetInternalPoint());
        cv::line(image_, cv_p1, cv_p2, cv_color, thickness);
    }

    void DrawRect(const Rect<int>& rect, const std::vector<double>& color, int thickness = 1) {
        cv::Scalar cv_color;
        if (!color.empty()) {
            if (image_.channels() >= 3) {
                // Convert RGB to BGR for OpenCV
                cv_color = cv::Scalar(color[2], color.size() > 1 ? color[1] : 0,
                                      color.size() > 2 ? color[0] : 0);
            } else {
                cv_color = cv::Scalar(color[0]);
            }
        }
        cv::Rect cv_rect = *static_cast<cv::Rect*>(rect.GetInternalRect());
        cv::rectangle(image_, cv_rect, cv_color, thickness);
    }

    void DrawCircle(const Point<int>& center, int radius, const std::vector<double>& color,
                    int thickness = 1) {
        cv::Scalar cv_color;
        if (!color.empty()) {
            if (image_.channels() >= 3) {
                // Convert RGB to BGR for OpenCV
                cv_color = cv::Scalar(color[2], color.size() > 1 ? color[1] : 0,
                                      color.size() > 2 ? color[0] : 0);
            } else {
                cv_color = cv::Scalar(color[0]);
            }
        }
        cv::Point cv_center = *static_cast<cv::Point*>(center.GetInternalPoint());
        cv::circle(image_, cv_center, radius, cv_color, thickness);
    }

    void Fill(const Rect<int>& rect, const std::vector<double>& color) {
        cv::Scalar cv_color;
        if (!color.empty()) {
            if (image_.channels() >= 3) {
                // Convert RGB to BGR for OpenCV
                cv_color = cv::Scalar(color[2], color.size() > 1 ? color[1] : 0,
                                      color.size() > 2 ? color[0] : 0);
            } else {
                cv_color = cv::Scalar(color[0]);
            }
        }
        cv::Rect cv_rect = *static_cast<cv::Rect*>(rect.GetInternalRect());
        image_(cv_rect).setTo(cv_color);
    }

    Image GaussianBlur(int kernel_size, double sigma) const {
        Image result;
        cv::GaussianBlur(image_, result.impl_->image_, cv::Size(kernel_size, kernel_size), sigma,
                         sigma);
        return result;
    }

    Image Erode(int kernel_size, int iterations) const {
        Image result;
        if ((kernel_size & 1) == 0) ++kernel_size;
        cv::Mat element = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kernel_size, kernel_size));
        cv::erode(image_, result.impl_->image_, element, cv::Point(-1,-1), std::max(1, iterations));
        return result;
    }

    Image Dilate(int kernel_size, int iterations) const {
        Image result;
        if ((kernel_size & 1) == 0) ++kernel_size;
        cv::Mat element = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kernel_size, kernel_size));
        cv::dilate(image_, result.impl_->image_, element, cv::Point(-1,-1), std::max(1, iterations));
        return result;
    }

    Image Threshold(double thresh, double maxval, int type) const {
        Image result;
        cv::Mat result_mat;
        cv::threshold(image_, result_mat, thresh, maxval, type);
        result.impl_->image_ = result_mat;
        return result;
    }

    Image AbsDiff(const Image::Impl& other) const {
        Image result;
        cv::absdiff(image_, other.image_, result.impl_->image_);
        return result;
    }

    Image MeanChannels() const {
        if (image_.channels() == 1) return Clone();
        Image result;
        std::vector<cv::Mat> ch;
        cv::split(image_, ch);
        CV_Assert(!ch.empty());
        cv::Mat acc;
        ch[0].convertTo(acc, CV_32F);
        for (size_t i = 1; i < ch.size(); ++i) {
            cv::Mat tmp;
            ch[i].convertTo(tmp, CV_32F);
            acc += tmp;
        }
        acc *= (1.0f / static_cast<float>(ch.size()));
        acc.convertTo(result.impl_->image_, CV_8U);
        return result;
    }

    Image Blend(const Image::Impl& other, const Image::Impl& mask) const {
        Image result;
        // Ensure same size/channels between A and B
        CV_Assert(image_.size() == other.image_.size());
        CV_Assert(image_.channels() == other.image_.channels());
        const int cn = image_.channels();

        // Ensure 8U buffers
        cv::Mat A, B;
        if (image_.depth() != CV_8U) image_.convertTo(A, CV_8U); else A = image_;
        if (other.image_.depth() != CV_8U) other.image_.convertTo(B, CV_8U); else B = other.image_;
        if (A.channels() == 1 && cn == 3) cv::cvtColor(A, A, cv::COLOR_GRAY2BGR);
        if (B.channels() == 1 && cn == 3) cv::cvtColor(B, B, cv::COLOR_GRAY2BGR);

        // Single-channel 8U mask
        cv::Mat M1;
        if (mask.image_.channels() == 1) M1 = mask.image_;
        else cv::cvtColor(mask.image_, M1, cv::COLOR_BGR2GRAY);

#if 1
        // Reference per-pixel integer implementation (correct but less optimal)
        result.impl_->image_.create(A.rows, A.cols, A.type());
        const int width = A.cols;
        const int height = A.rows;
        for (int y = 0; y < height; ++y) {
            const uchar* pa = A.ptr<uchar>(y);
            const uchar* pb = B.ptr<uchar>(y);
            const uchar* pm = M1.ptr<uchar>(y);
            uchar* po = result.impl_->image_.ptr<uchar>(y);
            for (int x = 0; x < width; ++x) {
                int m = pm[x];
                int inv = 255 - m;
                if (cn == 1) {
                    int a0 = pa[x];
                    int b0 = pb[x];
                    int out0 = (m * a0 + inv * b0 + 127) / 255;
                    po[x] = static_cast<uchar>(out0);
                } else {
                    int idx = x * 3;
                    for (int c = 0; c < 3; ++c) {
                        int a0 = pa[idx + c];
                        int b0 = pb[idx + c];
                        int out0 = (m * a0 + inv * b0 + 127) / 255;
                        po[idx + c] = static_cast<uchar>(out0);
                    }
                }
            }
        }
        return result;
#else
        // Optimized path using OpenCV arithmetics in 16U; exact to per-pixel integer formula
        // out = (m * a + (255 - m) * b + 127) / 255
        cv::Mat A16, B16, M16;
        A.convertTo(A16, CV_16U);
        B.convertTo(B16, CV_16U);
        M1.convertTo(M16, CV_16U);

        cv::Mat Mx;
        if (cn == 1) {
            Mx = M16;
        } else {
            // replicate mask across channels (cn can be 3, 4, ...)
            std::vector<cv::Mat> mv(static_cast<size_t>(cn), M16);
            cv::merge(mv, Mx);
        }

        cv::Mat inv;
        cv::subtract(cv::Scalar::all(255), Mx, inv, cv::noArray(), CV_16U);

        cv::Mat p1, p2, sum;
        cv::multiply(Mx, A16, p1, 1, CV_16U);   // M*A
        cv::multiply(inv, B16, p2, 1, CV_16U);  // (255-M)*B
        cv::add(p1, p2, sum, cv::noArray(), CV_16U);
        cv::add(sum, cv::Scalar::all(127), sum, cv::noArray(), CV_16U); // for rounding

        cv::Mat out16;
        cv::divide(sum, 255, out16, 1, CV_16U); // integer division (floor)
        out16.convertTo(result.impl_->image_, CV_8U);
        return result;
#endif
    }

    explicit Impl(void* internal_data) {
        image_ = *static_cast<cv::Mat*>(internal_data);
    }

    void Print(std::ostream& os) const {
        const int N = 10;  // Threshold for truncated display
        if (image_.rows > N || image_.cols > N) {
            // For large matrices, show truncated view
            os << "[";
            for (int i = 0; i < std::min(3, image_.rows); i++) {
                if (i > 0)
                    os << " ";
                os << "[";
                // Show first 3 elements
                for (int j = 0; j < std::min(3, image_.cols); j++) {
                    if (j > 0)
                        os << " ";
                    if (image_.channels() == 1) {
                        os << (int)image_.at<uchar>(i, j);
                    } else {
                        os << "[";
                        for (int c = 0; c < image_.channels(); c++) {
                            if (c > 0)
                                os << " ";
                            os << (int)image_.at<cv::Vec3b>(i, j)[c];
                        }
                        os << "]";
                    }
                }
                if (image_.cols > 3)
                    os << " ... ";
                // Show last 3 elements if there are more columns
                if (image_.cols > 6) {
                    for (int j = image_.cols - 3; j < image_.cols; j++) {
                        if (image_.channels() == 1) {
                            os << (int)image_.at<uchar>(i, j) << " ";
                        } else {
                            os << "[";
                            for (int c = 0; c < image_.channels(); c++) {
                                if (c > 0)
                                    os << " ";
                                os << (int)image_.at<cv::Vec3b>(i, j)[c];
                            }
                            os << "] ";
                        }
                    }
                }
                os << "]\n";
            }
            if (image_.rows > 6) {
                os << "...\n";
                // Show last 3 rows
                for (int i = image_.rows - 3; i < image_.rows; i++) {
                    os << "[";
                    for (int j = 0; j < std::min(3, image_.cols); j++) {
                        if (j > 0)
                            os << " ";
                        if (image_.channels() == 1) {
                            os << (int)image_.at<uchar>(i, j);
                        } else {
                            os << "[";
                            for (int c = 0; c < image_.channels(); c++) {
                                if (c > 0)
                                    os << " ";
                                os << (int)image_.at<cv::Vec3b>(i, j)[c];
                            }
                            os << "]";
                        }
                    }
                    if (image_.cols > 3)
                        os << " ... ";
                    if (image_.cols > 6) {
                        for (int j = image_.cols - 3; j < image_.cols; j++) {
                            if (image_.channels() == 1) {
                                os << (int)image_.at<uchar>(i, j) << " ";
                            } else {
                                os << "[";
                                for (int c = 0; c < image_.channels(); c++) {
                                    if (c > 0)
                                        os << " ";
                                    os << (int)image_.at<cv::Vec3b>(i, j)[c];
                                }
                                os << "] ";
                            }
                        }
                    }
                    os << "]\n";
                }
            }
            os << "]\n";
        } else {
            // For small matrices, show full content
            os << cv::format(image_, cv::Formatter::FMT_PYTHON) << "\n";
        }
        os << "Size(H x W x C): " << image_.rows << " x " << image_.cols << " x "
           << image_.channels() << "\n";
    }

private:
    cv::Mat image_;
};

}  // namespace inspirecv

#endif  // INSPIRECV_IMPL_OPENCV_IMAGE_OPENCV_H
