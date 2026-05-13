#ifndef INSPIRECV_IMPL_OPENCV_TRANSFORM_MATRIX_OPENCV_H
#define INSPIRECV_IMPL_OPENCV_TRANSFORM_MATRIX_OPENCV_H

#include "opencv2/opencv.hpp"
#include "inspirecv/core/transform_matrix.h"

namespace inspirecv {
class TransformMatrix::Impl {
public:
    Impl(const Impl &other) {
        // Must determine the type！
        matrix_ = other.matrix_.clone();
    }

    Impl(const cv::Mat &matrix) {
        matrix_ = matrix.clone();
    }
    Impl() {
        matrix_ = cv::Mat::eye(3, 3, CV_32F);
    }
    Impl(float a11, float a12, float b1, float a21, float a22, float b2) {
        matrix_ = cv::Mat::zeros(3, 3, CV_32F);
        matrix_.at<float>(0, 0) = a11;
        matrix_.at<float>(0, 1) = a12;
        matrix_.at<float>(0, 2) = b1;
        matrix_.at<float>(1, 0) = a21;
        matrix_.at<float>(1, 1) = a22;
        matrix_.at<float>(1, 2) = b2;
        matrix_.at<float>(2, 0) = 0.0f;
        matrix_.at<float>(2, 1) = 0.0f;
        matrix_.at<float>(2, 2) = 1.0f;
    }
    ~Impl() = default;

    // Internal methods
    static std::unique_ptr<Impl> CreateFromOpenCV(const cv::Mat &matrix) {
        return std::make_unique<Impl>(matrix);
    }

    // Basic getters and setters
    const cv::Mat &GetNative() const {
        return matrix_;
    }
    cv::Mat &GetNative() {
        return matrix_;
    }

    // Basic getters and setters
    float Get(int row, int col) const {
        return matrix_.at<float>(row, col);
    }

    void Set(int row, int col, float value) {
        matrix_.at<float>(row, col) = value;
    }

    // Squeeze the matrix into a vector
    std::vector<float> Squeeze() const {
        std::vector<float> result;
        for (int i = 0; i < 9; i++) {
            result.push_back(matrix_.at<float>(i));
        }
        return result;
    }

    // Operator overloading for array-style access
    float operator[](int index) const {
        return matrix_.at<float>(index / 3, index % 3);
    }

    float &operator[](int index) {
        return matrix_.at<float>(index / 3, index % 3);
    }

    void *GetInternalMatrix() const {
        return (void *)(&matrix_);
    }

    // Basic operations
    bool IsIdentity() const {
        return cv::norm(matrix_, cv::Mat::eye(3, 3, CV_32F)) < 1e-6;
    }

    void SetIdentity() {
        matrix_ = cv::Mat::eye(3, 3, CV_32F);
    }

    void Invert() {
        matrix_ = matrix_.inv();
    }

    TransformMatrix GetInverse() const {
        cv::Mat result = matrix_.inv();
        return TransformMatrix(result.at<float>(0, 0), result.at<float>(0, 1),
                               result.at<float>(0, 2), result.at<float>(1, 0),
                               result.at<float>(1, 1), result.at<float>(1, 2));
    }

    void Translate(float dx, float dy) {
        cv::Mat trans = cv::Mat::eye(3, 3, CV_32F);
        trans.at<float>(0, 2) = dx;
        trans.at<float>(1, 2) = dy;
        matrix_ = trans * matrix_;
    }

    void Scale(float sx, float sy) {
        cv::Mat scale = cv::Mat::eye(3, 3, CV_32F);
        scale.at<float>(0, 0) = sx;
        scale.at<float>(1, 1) = sy;
        matrix_ = scale * matrix_;
    }

    void Rotate(float angle) {
        float rad = angle * CV_PI / 180.0f;
        float c = std::cos(rad);
        float s = std::sin(rad);
        cv::Mat rot = cv::Mat::eye(3, 3, CV_32F);
        rot.at<float>(0, 0) = c;
        rot.at<float>(0, 1) = -s;
        rot.at<float>(1, 0) = s;
        rot.at<float>(1, 1) = c;
        matrix_ = rot * matrix_;
    }

    TransformMatrix Multiply(const TransformMatrix &other) const {
        const cv::Mat &other_mat =
          static_cast<const cv::Mat &>(*static_cast<const cv::Mat *>(other.GetInternalMatrix()));
        cv::Mat result = matrix_ * other_mat;
        return TransformMatrix(result.at<float>(0, 0), result.at<float>(0, 1),
                               result.at<float>(0, 2), result.at<float>(1, 0),
                               result.at<float>(1, 1), result.at<float>(1, 2));
    }

    TransformMatrix Clone() const {
        return TransformMatrix(matrix_.at<float>(0, 0), matrix_.at<float>(0, 1),
                               matrix_.at<float>(0, 2), matrix_.at<float>(1, 0),
                               matrix_.at<float>(1, 1), matrix_.at<float>(1, 2));
    }

    static TransformMatrix Create() {
        return TransformMatrix();
    }
    static TransformMatrix Create(float a11, float a12, float b1, float a21, float a22, float b2) {
        return TransformMatrix(a11, a12, b1, a21, a22, b2);
    }

private:
    cv::Mat matrix_;
};
}  // namespace inspirecv

#endif  // INSPIRECV_IMPL_OPENCV_TRANSFORM_MATRIX_OPENCV_H
