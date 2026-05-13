#ifndef OKCV_GEOM_POINT_H_
#define OKCV_GEOM_POINT_H_

#include <okcv/geometry/geom_common.h>
#include <okcv/geometry/cv_transform_matrix.h>
#include <vector>

namespace okcv {

/**
 * @brief A class representing a point in 2D space.
 * @tparam T The type of the coordinates.
 */
template <typename T>
class Point {
public:
    Point() : x(0), y(0) {}
    Point(const T &x, const T &y) : x(x), y(y) {}

    inline Point operator+(const Point &p) const {
        return Point(x + p.x, y + p.y);
    }

    inline Point &operator+=(const Point &p) {
        this->x += p.x;
        this->y += p.y;
        return *this;
    }

    inline Point operator-(const Point &p) const {
        return Point(x - p.x, y - p.y);
    }

    inline Point &operator-=(const Point &p) {
        this->x -= p.x;
        this->y -= p.y;
        return *this;
    }

    inline Point operator*(int k) const {
        return Point(x * k, y * k);
    }

    inline Point operator*(float k) const {
        return Point(x * k, y * k);
    }

    inline Point operator*(double k) const {
        return Point(x * k, y * k);
    }

    inline Point &operator*=(int k) {
        this->x *= k;
        this->y *= k;
        return *this;
    }

    inline Point &operator*=(float k) {
        this->x *= k;
        this->y *= k;
        return *this;
    }

    inline Point &operator*=(double k) {
        this->x *= k;
        this->y *= k;
        return *this;
    }

    inline Point operator/(int k) const {
        return Point(x / k, y / k);
    }

    inline Point operator/(float k) const {
        return Point(x / k, y / k);
    }

    inline Point operator/(double k) const {
        return Point(x / k, y / k);
    }

    inline Point &operator/=(int k) {
        this->x /= k;
        this->y /= k;
        return *this;
    }

    inline Point &operator/=(float k) {
        this->x /= k;
        this->y /= k;
        return *this;
    }

    inline Point &operator/=(double k) {
        this->x /= k;
        this->y /= k;
        return *this;
    }

    inline bool operator==(const Point &p) const {
        return x == p.x && y == p.y;
    }

    template <typename S>
    inline Point<S> As() const {
        return Point<S>(x, y);
    }

    inline Point<int> Round() const {
        return Point<int>(static_cast<int>(x + 0.5), static_cast<int>(y + 0.5));
    }

    inline T Cross(const Point &p) const {
        return x * p.y - y * p.x;
    }

    inline T Dot(const Point &p) const {
        return x * p.x + y * p.y;
    }

    inline T SquaredDistanceToOrigin() const {
        return x * x + y * y;
    }

    inline double SquaredDistanceToOriginD() const {
        return static_cast<double>(x) * x + static_cast<double>(y) * y;
    }

    inline double Length() const {
        return std::sqrt(SquaredDistanceToOriginD());
    }

    inline T DistanceSqured(const Point &p) const {
        return Square(x - p.x) + Square(y - p.y);
    }

    inline double DistanceSquredD(const Point &p) const {
        return Square(static_cast<double>(x) - p.x) + Square(static_cast<double>(y) - p.y);
    }

    inline double Distance(const Point &p) const {
        return std::sqrt(DistanceSquredD(p));
    }

    inline Point Rotate(float angle) const {
        return Point(cos(angle) * x - sin(angle) * y, sin(angle) * x + cos(angle) * y);
    }

    inline Point Rotate(const Point &p, float angle) const {
        return (*this - p).Rotate(angle) + p;
    }

    float Radian(const Point &p) const {
        float len = Length();
        float p_len = p.Length();
        if (len < std::numeric_limits<float>::epsilon() ||
            p_len < std::numeric_limits<float>::epsilon())
            return 0;
        return std::acos(Dot(p) / len / p_len);
    }

    inline Point Transform(const TransformMatrix &matrix) const {
        Point p;
        p.x = x * matrix[0] + y * matrix[1] + matrix[2];
        p.y = x * matrix[3] + y * matrix[4] + matrix[5];
        return p;
    }

    T x;
    T y;
};

typedef Point<int> Point2i;
typedef Point<float> Point2f;
typedef Point<double> Point2d;

template <typename T>
inline std::ostream &operator<<(std::ostream &stream, const Point<T> &p) {
    stream << "(" << p.x << "," << p.y << ")";
    return stream;
}

template <typename T>
class Point3 {
public:
    Point3() : x(0), y(0), z(0) {}
    Point3(const T &x, const T &y, const T &z) : x(x), y(y), z(z) {}

    inline Point3 operator+(const Point3 &p) const {
        return Point3(x + p.x, y + p.y, z + p.z);
    }

    inline Point3 &operator+=(const Point3 &p) {
        this->x += p.x;
        this->y += p.y;
        this->z += p.z;
        return *this;
    }

    inline Point3 operator-(const Point3 &p) const {
        return Point3(x - p.x, y - p.y, z - p.z);
    }

    inline Point3 &operator-=(const Point3 &p) {
        this->x -= p.x;
        this->y -= p.y;
        this->z -= p.z;
        return *this;
    }

    inline Point3 operator*(int k) const {
        return Point3(x * k, y * k, z * k);
    }

    inline Point3 operator*(float k) const {
        return Point3(x * k, y * k, z * k);
    }

    inline Point3 operator*(double k) const {
        return Point3(x * k, y * k, z * k);
    }

    inline Point3 &operator*=(int k) {
        this->x *= k;
        this->y *= k;
        this->z *= k;
        return *this;
    }

    inline Point3 &operator*=(float k) {
        this->x *= k;
        this->y *= k;
        this->z *= k;
        return *this;
    }

    inline Point3 &operator*=(double k) {
        this->x *= k;
        this->y *= k;
        this->z *= k;
        return *this;
    }

    inline Point3 operator/(int k) const {
        return Point3(x / k, y / k, z / k);
    }

    inline Point3 operator/(float k) const {
        return Point3(x / k, y / k, z / k);
    }

    inline Point3 operator/(double k) const {
        return Point3(x / k, y / k, z / k);
    }

    inline Point3 &operator/=(int k) {
        this->x /= k;
        this->y /= k;
        this->z /= k;
        return *this;
    }

    inline Point3 &operator/=(float k) {
        this->x /= k;
        this->y /= k;
        this->z /= k;
        return *this;
    }

    inline Point3 &operator/=(double k) {
        this->x /= k;
        this->y /= k;
        this->z /= k;
        return *this;
    }

    inline bool operator==(const Point3 &p) const {
        return x == p.x && y == p.y && z == p.z;
    }

    template <typename S>
    inline Point3<S> As() const {
        return Point3<S>(x, y, z);
    }

    inline Point3<int> Round() const {
        return Point3<int>(static_cast<int>(x + 0.5), static_cast<int>(y + 0.5),
                           static_cast<int>(z + 0.5));
    }

    inline T LengthSquared() const {
        return x * x + y * y + z * z;
    }

    inline T Length() const {
        return std::sqrt(LengthSquared());
    }

    inline T Dot(const Point3 &p) const {
        return x * p.x + y * p.y + z * p.z;
    }

    inline Point3 Cross(const Point3 &p) {
        return Point3(y * p.z - p.y * z, p.x * z - x * p.z, x * p.y - p.x * y);
    }

    Point3 Rotate(const Point3 &axis, float angle) const;

    T x;
    T y;
    T z;
};

typedef Point3<int> Point3i;
typedef Point3<float> Point3f;
typedef Point3<double> Point3d;

/**
 * @brief Output a Point3 to a stream.
 * @tparam T The type of the coordinates.
 * @param stream The stream to output to.
 * @param p The Point3 to output.
 * @return The stream.
 */
template <typename T>
inline std::ostream &operator<<(std::ostream &stream, const Point3<T> &p) {
    stream << "(" << p.x << "," << p.y << "," << p.z << ")";
    return stream;
}

/**
 * @brief Calculates the mean of a vector of points.
 * @tparam T The type of the coordinates.
 * @param points The vector of points.
 * @return The mean point.
 */
template <typename T>
T MeanPoint(const std::vector<T> &points) {
    assert(points.size() > 0);
    T mean;
    for (const auto &p : points)
        mean += p;
    mean /= static_cast<int>(points.size());
    return mean;
}

/**
 * @brief Estimates a similarity transformation matrix from a set of point correspondences.
 * @param src_points The source points.
 * @param dst_points The destination points.
 * @param matrix The estimated transformation matrix.
 */
void SimilarityTransformEstimate(const std::vector<Point2f> &src_points,
                                 const std::vector<Point2f> &dst_points, TransformMatrix &matrix);

/**
 * @brief Applies a transformation matrix to a vector of points.
 * @param points The input vector of points to transform.
 * @param matrix The transformation matrix.
 * @return A vector of transformed points.
 */
inline std::vector<Point2f> ApplyTransformToPoints(const std::vector<Point2f> &points,
                                                   const TransformMatrix &matrix) {
    std::vector<Point2f> out_points(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        out_points[i].x = points[i].x * matrix[0] + points[i].y * matrix[1] + matrix[2];
        out_points[i].y = points[i].x * matrix[3] + points[i].y * matrix[4] + matrix[5];
    }
    return out_points;
}

inline std::vector<Point2i> ApplyTransformToPoints(const std::vector<Point2i> &points,
                                                   const TransformMatrix &matrix) {
    std::vector<Point2i> out_points(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        out_points[i].x = points[i].x * matrix[0] + points[i].y * matrix[1] + matrix[2];
        out_points[i].y = points[i].x * matrix[3] + points[i].y * matrix[4] + matrix[5];
    }
    return out_points;
}

}  // namespace okcv

#endif  // OKCV_GEOM_POINT_H_
