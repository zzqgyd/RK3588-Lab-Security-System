#ifndef INSPIRECV_IMPL_OPENCV_RECT_OPENCV_H
#define INSPIRECV_IMPL_OPENCV_RECT_OPENCV_H
#include "opencv2/opencv.hpp"
#include "inspirecv/core/rect.h"
#include "inspirecv/core/point.h"

namespace inspirecv {
template <typename T>
class Rect<T>::Impl {
public:
    Impl(const Impl &other) : rect_(other.rect_) {}
    Impl(T x, T y, T width, T height) : rect_(x, y, width, height) {}
    ~Impl() = default;

    // Internal methods
    static std::unique_ptr<Impl> CreateFromOpenCV(const cv::Rect_<T> &rect) {
        return std::make_unique<Impl>(rect.x, rect.y, rect.width, rect.height);
    }

    // Basic getters and setters
    T GetX() const {
        return rect_.x;
    }
    T GetY() const {
        return rect_.y;
    }
    T GetWidth() const {
        return rect_.width;
    }
    T GetHeight() const {
        return rect_.height;
    }

    void *GetInternalRect() const {
        return (void *)(&rect_);
    }

    void SetX(T x) {
        rect_.x = x;
    }

    void SetY(T y) {
        rect_.y = y;
    }

    void SetWidth(T width) {
        rect_.width = width;
    }

    void SetHeight(T height) {
        rect_.height = height;
    }

    // Boundary points
    Point<T> TopLeft() const {
        return Point<T>::Create(rect_.x, rect_.y);
    }

    Point<T> TopRight() const {
        return Point<T>::Create(rect_.x + rect_.width, rect_.y);
    }

    Point<T> BottomLeft() const {
        return Point<T>::Create(rect_.x, rect_.y + rect_.height);
    }

    Point<T> BottomRight() const {
        return Point<T>::Create(rect_.x + rect_.width, rect_.y + rect_.height);
    }

    Point<T> Center() const {
        return Point<T>::Create(rect_.x + rect_.width / 2, rect_.y + rect_.height / 2);
    }

    std::vector<Point<T>> ToFourVertices() const {
        std::vector<Point<T>> vertices;
        vertices.push_back(TopLeft());
        vertices.push_back(TopRight());
        vertices.push_back(BottomRight());
        vertices.push_back(BottomLeft());
        return vertices;
    }

    // Get a safe rectangle that is bounded by the given width and height
    Rect<T> SafeRect(T width, T height) const {
        cv::Rect_<T> safe_rect = rect_;
        // Ensure x and width are within bounds
        if (safe_rect.x < 0) {
            safe_rect.width += safe_rect.x;
            safe_rect.x = 0;
        }
        if (safe_rect.x + safe_rect.width > width) {
            safe_rect.width = width - safe_rect.x;
        }

        // Ensure y and height are within bounds
        if (safe_rect.y < 0) {
            safe_rect.height += safe_rect.y;
            safe_rect.y = 0;
        }
        if (safe_rect.y + safe_rect.height > height) {
            safe_rect.height = height - safe_rect.y;
        }
        return Rect<T>(safe_rect.x, safe_rect.y, safe_rect.width, safe_rect.height);
    }

    T Area() const {
        return rect_.area();
    }

    bool Empty() const {
        return rect_.empty();
    }

    bool Contains(const Point<T> &point) const {
        return rect_.contains(cv::Point_<T>(point.GetX(), point.GetY()));
    }

    bool Contains(const Rect<T> &rect) const {
        cv::Rect_<T> other(rect.GetX(), rect.GetY(), rect.GetWidth(), rect.GetHeight());
        return (rect_ & other) == other;
    }

    // Geometric operations
    Rect<T> Intersect(const Rect<T> &other) const {
        cv::Rect_<T> otherRect(other.GetX(), other.GetY(), other.GetWidth(), other.GetHeight());
        auto result = rect_ & otherRect;
        return Rect<T>(result.x, result.y, result.width, result.height);
    }

    Rect<T> Union(const Rect<T> &other) const {
        cv::Rect_<T> otherRect(other.GetX(), other.GetY(), other.GetWidth(), other.GetHeight());
        auto result = rect_ | otherRect;
        return Rect<T>(result.x, result.y, result.width, result.height);
    }

    double IoU(const Rect<T> &other) const {
        cv::Rect_<T> otherRect(other.GetX(), other.GetY(), other.GetWidth(), other.GetHeight());
        cv::Rect_<T> intersection = rect_ & otherRect;
        // Calculate actual union area
        double union_area = rect_.area() + otherRect.area() - intersection.area();
        if (union_area == 0)
            return 0.0;
        return static_cast<double>(intersection.area()) / union_area;
    }

    void Scale(T sx, T sy) {
        rect_.x = static_cast<T>(rect_.x * sx);
        rect_.y = static_cast<T>(rect_.y * sy);
        rect_.width = static_cast<T>(rect_.width * sx);
        rect_.height = static_cast<T>(rect_.height * sy);
    }

    void Translate(T dx, T dy) {
        rect_.x = static_cast<T>(rect_.x + dx);
        rect_.y = static_cast<T>(rect_.y + dy);
    }

    Rect<T> Square(float scale = 1.0) const {
        T center_x = rect_.x + rect_.width / 2;
        T center_y = rect_.y + rect_.height / 2;
        T size = std::max(rect_.width, rect_.height) * scale;
        return Rect<T>(center_x - size / 2, center_y - size / 2, size, size);
    }

    const cv::Rect_<T> &GetNative() const {
        return rect_;
    }
    cv::Rect_<T> &GetNative() {
        return rect_;
    }

private:
    cv::Rect_<T> rect_;
};
}  // namespace inspirecv

#endif  // INSPIRECV_IMPL_OPENCV_RECT_OPENCV_H
