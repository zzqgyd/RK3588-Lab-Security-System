#ifndef INSPIRECV_IMPL_OKCV_RECT_OKCV_H
#define INSPIRECV_IMPL_OKCV_RECT_OKCV_H
#include "okcv/okcv.h"
#include "inspirecv/core/rect.h"

namespace inspirecv {
template <typename T>
class Rect<T>::Impl {
public:
    Impl(const Impl &other) : rect_(other.rect_) {}
    Impl(T x, T y, T width, T height) : rect_(x, y, x + width, y + height) {}
    ~Impl() = default;

    // Internal methods
    static std::unique_ptr<Impl> CreateFromOKCV(const okcv::Rect<T> &rect) {
        return std::make_unique<Impl>(rect.xmin(), rect.ymin(), rect.GetWidth(), rect.GetHeight());
    }

    // Basic getters and setters
    T GetX() const {
        return rect_.xmin();
    }
    T GetY() const {
        return rect_.ymin();
    }
    T GetWidth() const {
        return rect_.GetWidth();
    }
    T GetHeight() const {
        return rect_.GetHeight();
    }

    void *GetInternalRect() const {
        return (void *)(&rect_);
    }

    void SetX(T x) {
        auto width = GetWidth();
        auto height = GetHeight();
        rect_.Set(x, GetY(), x + width, GetY() + height);
    }

    void SetY(T y) {
        auto width = GetWidth();
        auto height = GetHeight();
        rect_.Set(GetX(), y, GetX() + width, y + height);
    }

    void SetWidth(T width) {
        rect_.Set(GetX(), GetY(), GetX() + width, GetY() + GetHeight());
    }

    void SetHeight(T height) {
        rect_.Set(GetX(), GetY(), GetX() + GetWidth(), GetY() + height);
    }

    // Boundary points
    Point<T> TopLeft() const {
        return Point<T>::Create(rect_.xmin(), rect_.ymin());
    }

    Point<T> TopRight() const {
        return Point<T>::Create(rect_.xmax(), rect_.ymin());
    }

    Point<T> BottomLeft() const {
        return Point<T>::Create(rect_.xmin(), rect_.ymax());
    }

    Point<T> BottomRight() const {
        return Point<T>::Create(rect_.xmax(), rect_.ymax());
    }

    Point<T> Center() const {
        auto center = rect_.GetCenter();
        return Point<T>::Create(center.x, center.y);
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
        okcv::Rect<T> safe_rect = rect_;

        // Ensure x and width are within bounds
        if (safe_rect.xmin() < 0) {
            safe_rect.Set(0, safe_rect.ymin(), safe_rect.xmax(), safe_rect.ymax());
        }
        if (safe_rect.xmax() > width) {
            safe_rect.Set(safe_rect.xmin(), safe_rect.ymin(), width, safe_rect.ymax());
        }

        // Ensure y and height are within bounds
        if (safe_rect.ymin() < 0) {
            safe_rect.Set(safe_rect.xmin(), 0, safe_rect.xmax(), safe_rect.ymax());
        }
        if (safe_rect.ymax() > height) {
            safe_rect.Set(safe_rect.xmin(), safe_rect.ymin(), safe_rect.xmax(), height);
        }

        return Rect<T>(safe_rect.xmin(), safe_rect.ymin(), safe_rect.GetWidth(),
                       safe_rect.GetHeight());
    }

    // Basic operations
    T Area() const {
        return rect_.GetArea();
    }
    bool Empty() const {
        return rect_.IsEmpty();
    }

    bool Contains(const Point<T> &point) const {
        return point.GetX() >= rect_.xmin() && point.GetX() <= rect_.xmax() &&
               point.GetY() >= rect_.ymin() && point.GetY() <= rect_.ymax();
    }

    bool Contains(const Rect<T> &rect) const {
        okcv::Rect<T> other(rect.GetX(), rect.GetY(), rect.GetX() + rect.GetWidth(),
                            rect.GetY() + rect.GetHeight());
        return rect_.Contains(other);
    }

    // Geometric operations
    Rect<T> Intersect(const Rect<T> &other) const {
        okcv::Rect<T> otherRect(other.GetX(), other.GetY(), other.GetX() + other.GetWidth(),
                                other.GetY() + other.GetHeight());
        auto result = rect_.Intersect(otherRect);
        return Rect<T>(result.xmin(), result.ymin(), result.GetWidth(), result.GetHeight());
    }

    Rect<T> Union(const Rect<T> &other) const {
        okcv::Rect<T> otherRect(other.GetX(), other.GetY(), other.GetX() + other.GetWidth(),
                                other.GetY() + other.GetHeight());
        auto result = rect_.UnionRect(otherRect);
        return Rect<T>(result.xmin(), result.ymin(), result.GetWidth(), result.GetHeight());
    }

    double IoU(const Rect<T> &other) const {
        okcv::Rect<T> otherRect(other.GetX(), other.GetY(), other.GetX() + other.GetWidth(),
                                other.GetY() + other.GetHeight());
        auto intersect_area = rect_.IntersectArea(otherRect);
        auto union_area = rect_.UnionArea(otherRect);

        return rect_.IoU(otherRect);
    }

    // Transformation operations
    void Scale(T sx, T sy) {
        auto center = rect_.GetCenter();
        auto width = rect_.GetWidth();
        auto height = rect_.GetHeight();
        rect_.Set(rect_.xmin() * sx, rect_.ymin() * sy, rect_.xmax() * sx, rect_.ymax() * sy);
    }

    void Translate(T dx, T dy) {
        rect_.Set(rect_.xmin() + dx, rect_.ymin() + dy, rect_.xmax() + dx, rect_.ymax() + dy);
    }

    Rect<T> Square(float scale = 1.0) const {
        auto result = rect_.Square(scale);
        return Rect<T>(result.xmin(), result.ymin(), result.GetWidth(), result.GetHeight());
    }

    // Access to native OKCV rect
    const okcv::Rect<T> &GetNative() const {
        return rect_;
    }
    okcv::Rect<T> &GetNative() {
        return rect_;
    }

private:
    okcv::Rect<T> rect_;
};
}  // namespace inspirecv
#endif
