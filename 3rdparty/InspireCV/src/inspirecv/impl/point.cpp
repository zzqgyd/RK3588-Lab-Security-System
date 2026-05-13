
#include "impl.h"
#include <memory>
#include <cmath>

#ifdef INSPIRECV_BACKEND_OPENCV
#include "utils/similar_transform_umeyama_opencv.h"
#else 
#include "utils/similar_transform_umeyama_eigen.h"
#endif

namespace inspirecv {

template <typename T>
Point<T>::Point() : impl_(std::make_unique<Impl>(0, 0)) {}

template <typename T>
Point<T>::Point(T x, T y) : impl_(std::make_unique<Impl>(x, y)) {}

template <typename T>
Point<T>::Point(const Point &other)
: impl_(std::make_unique<Impl>(other.impl_->GetX(), other.impl_->GetY())) {}

template <typename T>
Point<T> &Point<T>::operator=(const Point &other) {
    if (this != &other) {
        impl_ = std::make_unique<Impl>(other.impl_->GetX(), other.impl_->GetY());
    }
    return *this;
}

template <typename T>
Point<T>::~Point() = default;

template <typename T>
Point<T>::Point(Point &&) noexcept = default;

template <typename T>
Point<T> &Point<T>::operator=(Point &&) noexcept = default;

template <typename T>
bool Point<T>::operator==(const Point &other) const {
    return impl_->GetX() == other.impl_->GetX() && impl_->GetY() == other.impl_->GetY();
}

template <typename T>
T Point<T>::GetX() const {
    return impl_->GetX();
}

template <typename T>
T Point<T>::GetY() const {
    return impl_->GetY();
}

template <typename T>
void Point<T>::SetX(T x) {
    impl_->SetX(x);
}

template <typename T>
void Point<T>::SetY(T y) {
    impl_->SetY(y);
}

template <typename T>
void *Point<T>::GetInternalPoint() const {
    return impl_->GetInternalPoint();
}

template <typename T>
double Point<T>::Length() const {
    return impl_->Length();
}

template <typename T>
double Point<T>::Distance(const Point &other) const {
    return impl_->Distance(*other.impl_);
}

template <typename T>
T Point<T>::Dot(const Point &other) const {
    return impl_->Dot(*other.impl_);
}

template <typename T>
T Point<T>::Cross(const Point &other) const {
    return impl_->Cross(*other.impl_);
}

template <typename T>
template <typename U>
Point<U> Point<T>::As() const {
    return Point<U>(static_cast<U>(impl_->GetX()), static_cast<U>(impl_->GetY()));
}

template <typename T>
std::ostream &operator<<(std::ostream &os, const Point<T> &point) {
    os << "Point(" << point.GetX() << ", " << point.GetY() << ")";
    return os;
}

template <typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<Point<T>> &points) {
    os << "[\n";
    for (size_t i = 0; i < points.size(); i++) {
        os << "(" << points[i].GetX() << ", " << points[i].GetY() << ")";
        if (i < points.size() - 1) {
            os << ",\n";
        }
    }
    os << "]\n";
    os << "Num of Points: " << points.size();
    return os;
}

template <typename T>
std::vector<Point<T>> ApplyTransformToPoints(const std::vector<Point<T>> &points,
                                             const TransformMatrix &transform) {
    std::vector<Point<T>> output(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        T x = points[i].GetX() * transform.Get(0, 0) + points[i].GetY() * transform.Get(0, 1) +
              transform.Get(0, 2);
        T y = points[i].GetX() * transform.Get(1, 0) + points[i].GetY() * transform.Get(1, 1) +
              transform.Get(1, 2);
        output[i] = Point<T>::Create(x, y);
    }
    return output;
}

template <typename T>
TransformMatrix SimilarityTransformEstimate(const std::vector<Point<T>> &src_points,
                                            const std::vector<Point<T>> &dst_points) {
    INSPIRECV_CHECK(src_points.size() == dst_points.size());
    // Calculate mean points
    double src_mean_x = 0, src_mean_y = 0;  // Changed to double
    double dst_mean_x = 0, dst_mean_y = 0;  // Changed to double
    for (size_t i = 0; i < src_points.size(); i++) {
        src_mean_x += static_cast<double>(src_points[i].GetX());  // Cast to double
        src_mean_y += static_cast<double>(src_points[i].GetY());  // Cast to double
        dst_mean_x += static_cast<double>(dst_points[i].GetX());  // Cast to double
        dst_mean_y += static_cast<double>(dst_points[i].GetY());  // Cast to double
    }
    src_mean_x /= static_cast<double>(src_points.size());  // Cast size to double
    src_mean_y /= static_cast<double>(src_points.size());
    dst_mean_x /= static_cast<double>(dst_points.size());
    dst_mean_y /= static_cast<double>(dst_points.size());

    // Calculate transformation parameters
    double src_norm2 = 0;  // Changed to double
    double sum_a = 0;      // Changed to double
    double sum_b = 0;      // Changed to double
    for (size_t i = 0; i < src_points.size(); i++) {
        double src_dx = static_cast<double>(src_points[i].GetX()) - src_mean_x;  // Cast to double
        double src_dy = static_cast<double>(src_points[i].GetY()) - src_mean_y;
        double dst_dx = static_cast<double>(dst_points[i].GetX()) - dst_mean_x;
        double dst_dy = static_cast<double>(dst_points[i].GetY()) - dst_mean_y;

        src_norm2 += src_dx * src_dx + src_dy * src_dy;
        sum_a += src_dx * dst_dx + src_dy * dst_dy;
        sum_b += src_dx * dst_dy - src_dy * dst_dx;
    }

    TransformMatrix matrix;
    if (std::fabs(src_norm2) <
        std::numeric_limits<double>::epsilon()) {  // Changed to double epsilon
        double a = 1;                              // Changed to double
        double b = 0;
        double tx = dst_mean_x - src_mean_x;
        double ty = dst_mean_y - src_mean_y;
        matrix.Set(0, 0, a);
        matrix.Set(0, 1, -b);
        matrix.Set(0, 2, tx);
        matrix.Set(1, 0, b);
        matrix.Set(1, 1, a);
        matrix.Set(1, 2, ty);
    } else {
        double a = sum_a / src_norm2;  // Changed to double
        double b = sum_b / src_norm2;
        double tx = dst_mean_x - (a * src_mean_x - b * src_mean_y);
        double ty = dst_mean_y - (b * src_mean_x + a * src_mean_y);
        matrix.Set(0, 0, a);
        matrix.Set(0, 1, -b);
        matrix.Set(0, 2, tx);
        matrix.Set(1, 0, b);
        matrix.Set(1, 1, a);
        matrix.Set(1, 2, ty);
    }
    return matrix;
}

template <typename T>
TransformMatrix SimilarityTransformEstimateUmeyama(const std::vector<Point<T>> &src_points,
                                                   const std::vector<Point<T>> &dst_points) {
    INSPIRECV_CHECK_EQ(src_points.size(), dst_points.size());
    std::vector<float> src_vec;
    std::vector<float> dst_vec;
    for (size_t i = 0; i < src_points.size(); i++) {
        src_vec.push_back(src_points[i].GetX());
        src_vec.push_back(src_points[i].GetY());
        dst_vec.push_back(dst_points[i].GetX());
        dst_vec.push_back(dst_points[i].GetY());
    }
#ifdef INSPIRECV_BACKEND_OPENCV
    cv::Mat src_mat(5, 2, CV_32F, src_vec.data());
    cv::Mat dst_mat(5, 2, CV_32F, dst_vec.data());
    auto temp = similarTransform(src_mat, dst_mat);
    cv::Mat M = temp.rowRange(0, 2);
    auto mat = TransformMatrix::Create(M.at<float>(0), M.at<float>(1), M.at<float>(2),
                                   M.at<float>(3), M.at<float>(4), M.at<float>(5));
    mat.Invert();
    return mat;
#else
    
    auto result = SimilarTransform(src_vec, dst_vec);
    auto mat = TransformMatrix::Create(result[0], result[1], result[2], result[3], result[4], result[5]);
    mat.Invert();
    return mat;
#endif
}

template Point<int> Point<float>::As<int>() const;
template Point<float> Point<int>::As<float>() const;
template Point<double> Point<float>::As<double>() const;
template Point<float> Point<double>::As<float>() const;
template Point<int> Point<double>::As<int>() const;
template Point<double> Point<int>::As<double>() const;

template class Point<int>;
template class Point<float>;
template class Point<double>;

template std::ostream &operator<<(std::ostream &os, const Point<int> &point);
template std::ostream &operator<<(std::ostream &os, const Point<float> &point);
template std::ostream &operator<<(std::ostream &os, const Point<double> &point);

template std::ostream &operator<<(std::ostream &os, const std::vector<Point<int>> &points);
template std::ostream &operator<<(std::ostream &os, const std::vector<Point<float>> &points);
template std::ostream &operator<<(std::ostream &os, const std::vector<Point<double>> &points);

template std::vector<Point<int>> ApplyTransformToPoints(const std::vector<Point<int>> &points,
                                                        const TransformMatrix &transform);
template std::vector<Point<float>> ApplyTransformToPoints(const std::vector<Point<float>> &points,
                                                          const TransformMatrix &transform);
template std::vector<Point<double>> ApplyTransformToPoints(const std::vector<Point<double>> &points,
                                                           const TransformMatrix &transform);
template TransformMatrix SimilarityTransformEstimate(const std::vector<Point<int>> &src_points,
                                                     const std::vector<Point<int>> &dst_points);
template TransformMatrix SimilarityTransformEstimate(const std::vector<Point<float>> &src_points,
                                                     const std::vector<Point<float>> &dst_points);
template TransformMatrix SimilarityTransformEstimate(const std::vector<Point<double>> &src_points,
                                                     const std::vector<Point<double>> &dst_points);
template TransformMatrix SimilarityTransformEstimateUmeyama(const std::vector<Point<int>> &src_points,
                                                     const std::vector<Point<int>> &dst_points);
template TransformMatrix SimilarityTransformEstimateUmeyama(const std::vector<Point<float>> &src_points,
                                                     const std::vector<Point<float>> &dst_points);
template TransformMatrix SimilarityTransformEstimateUmeyama(const std::vector<Point<double>> &src_points,
                                                     const std::vector<Point<double>> &dst_points);
                                                     
}  // namespace inspirecv
