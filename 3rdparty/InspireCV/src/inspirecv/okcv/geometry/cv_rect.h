#ifndef OKCV_GEOM_RECT_H_
#define OKCV_GEOM_RECT_H_

#include <okcv/geometry/geom_common.h>
#include <okcv/geometry/cv_point.h>
#include <okcv/geometry/cv_transform_matrix.h>
#include <okcv/geometry/cv_size.h>
#include <okcv/geometry/cv_point.h>

namespace okcv {

/**
 * @brief A class representing a rectangle.
 * @tparam T The type of the coordinates.
 */
template <typename T>
class Rect {
public:
    Rect() : xmin_(0), ymin_(0), xmax_(0), ymax_(0) {}
    Rect(const T &xmin, const T &ymin, const T &xmax, const T &ymax)
    : xmin_(xmin), ymin_(ymin), xmax_(xmax), ymax_(ymax) {}

    Rect(const T &xmin, const T &ymin, const Size<T> &size)
    : xmin_(xmin), ymin_(ymin), xmax_(xmin + size.Width()), ymax_(ymin + size.Height()) {}

    template <typename S>
    inline Rect<S> As() const {
        return Rect<S>(xmin_, ymin_, xmax_, ymax_);
    }

    inline void Set(T xmin, T ymin, T xmax, T ymax) {
        xmin_ = xmin;
        ymin_ = ymin;
        xmax_ = xmax;
        ymax_ = ymax;
    }

    inline T GetWidth() const {
        return xmax_ - xmin_;
    }

    inline T GetHeight() const {
        return ymax_ - ymin_;
    }

    inline T GetArea() const {
        T width = GetWidth();
        T height = GetHeight();
        if (width <= 0 || height <= 0)
            return 0;
        return width * height;
    }

    inline std::vector<Point<T>> ToPoints() const {
        return {TopLeft(), TopRight(), BottomRight(), BottomLeft()};
    }

    inline Rect Square(float scale) const {
        auto center = GetCenter();
        auto half_side = std::max(GetWidth(), GetHeight()) / 2.0;  // 使用宽高的最大值
        return Rect(center.x - half_side * scale, center.y - half_side * scale,
                    center.x + half_side * scale, center.y + half_side * scale);
    }

    inline bool IsEmpty() const {
        return xmax_ <= xmin_ || ymax_ <= ymin_;
    }

    inline T GetCenterX() const {
        return xmin_ + (xmax_ - xmin_) / 2;
    }

    inline T GetCenterY() const {
        return ymin_ + (ymax_ - ymin_) / 2;
    }

    inline Point<T> GetCenter() const {
        return Point<T>(GetCenterX(), GetCenterY());
    }

    inline Point<T> TopLeft() const {
        return Point<T>(xmin_, ymin_);
    }

    inline Point<T> TopRight() const {
        return Point<T>(xmax_, ymin_);
    }

    inline Point<T> BottomLeft() const {
        return Point<T>(xmin_, ymax_);
    }

    inline Point<T> BottomRight() const {
        return Point<T>(xmax_, ymax_);
    }

    inline Rect Intersect(const Rect &rect) const {
        return Rect(std::max(xmin_, rect.xmin_), std::max(ymin_, rect.ymin_),
                    std::min(xmax_, rect.xmax_), std::min(ymax_, rect.ymax_));
    }

    inline Rect UnionRect(const Rect &rect) const {
        return Rect(std::min(xmin_, rect.xmin_), std::min(ymin_, rect.ymin_),
                    std::max(xmax_, rect.xmax_), std::max(ymax_, rect.ymax_));
    }

    inline T IntersectArea(const Rect &rect) const {
        T width = std::min(xmax_, rect.xmax_) - std::max(xmin_, rect.xmin_);
        T height = std::min(ymax_, rect.ymax_) - std::max(ymin_, rect.ymin_);
        if (width <= 0 || height <= 0)
            return 0;
        return width * height;
    }

    inline T UnionArea(const Rect &rect) const {
        return GetArea() + rect.GetArea() - IntersectArea(rect);
    }

    inline float IoU(const Rect &rect) const {
        T intersect_area = IntersectArea(rect);
        T union_area = UnionArea(rect);
        if (union_area == 0)
            return 0;
        return static_cast<float>(intersect_area) / union_area;
    }

    inline bool Contains(const Rect &rect) const {
        return xmin_ <= rect.xmin_ && xmax_ >= rect.xmax_ && ymin_ <= rect.ymin_ &&
               ymax_ >= rect.ymax_;
    }

    inline void ScaleX(float scale_x) {
        auto x_center = GetCenterX();
        auto half_width = GetWidth() / 2.0;
        xmin_ = x_center - half_width * scale_x;
        xmax_ = x_center + half_width * scale_x;
    }

    inline void ScaleY(float scale_y) {
        auto y_center = GetCenterY();
        auto half_height = GetHeight() / 2.0;
        ymin_ = y_center - half_height * scale_y;
        ymax_ = y_center + half_height * scale_y;
    }

    inline void Scale(float scale_x, float scale_y) {
        ScaleX(scale_x);
        ScaleY(scale_y);
    }

    inline void ScaleOutToAspectRatio(T target_width, T target_height) {
        auto width = GetWidth();
        auto height = GetHeight();
        if (width * target_height < height * target_width) {
            width = height * target_width / target_height;
            xmin_ = GetCenterX() - width / 2;
            xmax_ = xmin_ + width;
        } else {
            height = width * target_height / target_width;
            ymin_ = GetCenterY() - height / 2;
            ymax_ = ymin_ + height;
        }
    }

    inline void ScaleOutToAspectRatioFromOrigin(T target_width, T target_height) {
        auto width = GetWidth();
        auto height = GetHeight();
        if (width * target_height < height * target_width) {
            width = height * target_width / target_height;
            xmax_ = xmin_ + width;
        } else {
            height = width * target_height / target_width;
            ymax_ = ymin_ + height;
        }
    }

    inline Rect Transform(const TransformMatrix &matrix) const {
        Rect rect;
        T x1 = xmin_ * matrix[0] + ymin_ * matrix[1] + matrix[2];
        T y1 = xmin_ * matrix[3] + ymin_ * matrix[4] + matrix[5];
        T x2 = xmax_ * matrix[0] + ymax_ * matrix[1] + matrix[2];
        T y2 = xmax_ * matrix[3] + ymax_ * matrix[4] + matrix[5];
        rect.xmin_ = std::min(x1, x2);
        rect.ymin_ = std::min(y1, y2);
        rect.xmax_ = std::max(x1, x2);
        rect.ymax_ = std::max(y1, y2);
        return rect;
    }

    inline void ScaleOrigin(float scale_x, float scale_y) {
        xmin_ *= scale_x;
        xmax_ *= scale_x;
        ymin_ *= scale_y;
        ymax_ *= scale_y;
    }

    inline void ScaleOrigin(float scale) {
        xmin_ *= scale;
        xmax_ *= scale;
        ymin_ *= scale;
        ymax_ *= scale;
    }

    inline Rect<int> Round() const {
        return Rect<int>(std::round(xmin_), std::round(ymin_), std::round(xmax_),
                         std::round(ymax_));
    }

    T xmin() const {
        return xmin_;
    }
    T ymin() const {
        return ymin_;
    }
    T xmax() const {
        return xmax_;
    }
    T ymax() const {
        return ymax_;
    }

    inline void SetXMin(const T &xmin) {
        xmin_ = xmin;
    }
    inline void SetYMin(const T &ymin) {
        ymin_ = ymin;
    }
    inline void SetXMax(const T &xmax) {
        xmax_ = xmax;
    }
    inline void SetYMax(const T &ymax) {
        ymax_ = ymax;
    }

private:
    T xmin_;
    T ymin_;
    T xmax_;
    T ymax_;
};

/**
 * @brief Outputs a rectangle to a stream.
 * @tparam T The type of the coordinates.
 * @param stream The stream to output to.
 * @param rect The rectangle to output.
 * @return The stream.
 */
template <typename T>
inline std::ostream &operator<<(std::ostream &stream, const Rect<T> &rect) {
    stream << "[" << rect.xmin() << "," << rect.ymin() << "," << rect.xmax() << "," << rect.ymax()
           << "]";
    return stream;
}

typedef Rect<int> Rect2i;
typedef Rect<float> Rect2f;
typedef Rect<double> Rect2d;

/**
 * @brief Calculates the minimum bounding rectangle for a set of points.
 * @tparam T The type of the coordinates.
 * @param points The vector of points.
 * @return The minimum bounding rectangle.
 */
template <typename T>
Rect<T> MinBoundingRect(const std::vector<Point<T>> &points) {
    assert(points.size() >= 2);

    T xmin = points[0].x;
    T ymin = points[0].y;
    T xmax = xmin;
    T ymax = ymin;
    for (const auto &p : points) {
        xmin = std::min(p.x, xmin);
        ymin = std::min(p.y, ymin);
        xmax = std::max(p.x, xmax);
        ymax = std::max(p.y, ymax);
    }
    return Rect<T>(xmin, ymin, xmax, ymax);
}

/**
 * @brief Estimates a similarity transformation matrix between two rectangles.
 * @tparam T The type of the coordinates.
 * @param src_rect The source rectangle.
 * @param dst_rect The destination rectangle.
 * @param matrix The estimated transformation matrix.
 */
template <typename T>
inline void SimilarityTransformEstimate(const Rect<T> &src_rect, const Rect<T> &dst_rect,
                                        TransformMatrix &matrix) {
    auto src_points = src_rect.ToPoints();
    auto dst_points = dst_rect.ToPoints();
    SimilarityTransformEstimate(src_points, dst_points, matrix);
}

/**
 * @brief Normalizes the shape of a vector of points to a square.
 * @tparam T The type of the coordinates.
 * @param points The vector of points.
 * @param mean_points The mean points.
 * @return The normalized points.
 */
inline std::vector<Point2f> PointsShapeNormalizationSquare(
  const std::vector<Point2f> &points, const std::vector<Point2f> &mean_points) {
    Rect2f src_rect = MinBoundingRect(points).Square(1.0);
    Rect2f dst_rect = MinBoundingRect(mean_points).Square(1.0);
    TransformMatrix matrix;
    SimilarityTransformEstimate(src_rect, dst_rect, matrix);
    return ApplyTransformToPoints(points, matrix);
}

}  // namespace okcv

#endif  // OKCV_GEOM_RECT_H_
