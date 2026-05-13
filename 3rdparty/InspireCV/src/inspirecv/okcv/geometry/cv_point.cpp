#include "cv_point.h"
#include "check.h"

namespace okcv {

// dst == src * matrix
void SimilarityTransformEstimate(const std::vector<Point2f> &src_points,
                                 const std::vector<Point2f> &dst_points, TransformMatrix &matrix) {
    INSPIRECV_CHECK(src_points.size() == dst_points.size())
      << src_points.size() << " " << dst_points.size();
    Point2f src_mean = MeanPoint(src_points);
    Point2f dst_mean = MeanPoint(dst_points);

    float src_norm2 = 0.f;
    float sum_a = 0.f;
    float sum_b = 0.f;
    for (int i = 0; i < src_points.size(); i++) {
        Point2f src_d = src_points[i] - src_mean;
        Point2f dst_d = dst_points[i] - dst_mean;

        src_norm2 += src_d.x * src_d.x + src_d.y * src_d.y;
        sum_a += src_d.x * dst_d.x + src_d.y * dst_d.y;
        sum_b += src_d.x * dst_d.y - src_d.y * dst_d.x;
    }

    if (std::fabs(src_norm2) < std::numeric_limits<float>::epsilon()) {
        float a = 1.f;
        float b = 0.f;
        float tx = dst_mean.x - src_mean.x;
        float ty = dst_mean.y - src_mean.y;
        matrix[0] = a;
        matrix[1] = -b;
        matrix[2] = tx;
        matrix[3] = b;
        matrix[4] = a;
        matrix[5] = ty;
    } else {
        float a = sum_a / src_norm2;
        float b = sum_b / src_norm2;
        float tx = dst_mean.x - (a * src_mean.x - b * src_mean.y);
        float ty = dst_mean.y - (b * src_mean.x + a * src_mean.y);
        matrix[0] = a;
        matrix[1] = -b;
        matrix[2] = tx;
        matrix[3] = b;
        matrix[4] = a;
        matrix[5] = ty;
    }
}

template <typename T>
Point3<T> Point3<T>::Rotate(const Point3<T> &axis, float angle) const {
    double length = axis.Length();
    if (IsNearlyEqual(length, 0., 1e-8)) {
        INSPIRECV_LOG(WARN) << "Point3f Rotate falied!";
        return Point3(x, y, z);
    }
    auto normal_axis = axis / length;
    std::vector<std::vector<T>> mat(3, std::vector<T>(3, 0));
    mat[0][0] = mat[1][1] = mat[2][2] = 1.0;

    T u = normal_axis.x;
    T v = normal_axis.y;
    T w = normal_axis.z;

    mat[0][0] = std::cos(angle) + (u * u) * (1 - std::cos(angle));
    mat[0][1] = u * v * (1 - std::cos(angle)) - w * std::sin(angle);
    mat[0][2] = u * w * (1 - std::cos(angle)) + v * std::sin(angle);

    mat[1][0] = u * v * (1 - std::cos(angle)) + w * std::sin(angle);
    mat[1][1] = std::cos(angle) + v * v * (1 - std::cos(angle));
    mat[1][2] = v * w * (1 - std::cos(angle)) - u * std::sin(angle);

    mat[2][0] = u * w * (1 - std::cos(angle)) - v * std::sin(angle);
    mat[2][1] = w * v * (1 - std::cos(angle)) + u * std::sin(angle);
    mat[2][2] = std::cos(angle) + w * w * (1 - std::cos(angle));

    Point3 rotated_joint;
    rotated_joint.x = mat[0][0] * x + mat[0][1] * y + mat[0][2] * z;
    rotated_joint.y = mat[1][0] * x + mat[1][1] * y + mat[1][2] * z;
    rotated_joint.z = mat[2][0] * x + mat[2][1] * y + mat[2][2] * z;

    return rotated_joint;
}

}  // namespace okcv
