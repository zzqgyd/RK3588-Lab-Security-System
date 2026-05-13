#include "../common/common.h"
#include <inspirecv/inspirecv.h>

TEST_CASE("test_points", "[geometry]") {
    DRAW_SPLIT_LINE;
    SECTION("Default Constructor") {
        inspirecv::Point2f point;
        REQUIRE(point.GetX() == Approx(0.0f));
        REQUIRE(point.GetY() == Approx(0.0f));
    }

    SECTION("Parameterized Constructor") {
        inspirecv::Point2f point(3.14f, 2.718f);
        REQUIRE(point.GetX() == Approx(3.14f));
        REQUIRE(point.GetY() == Approx(2.718f));
    }

    SECTION("Factory Method") {
        auto point = inspirecv::Point2f::Create(1.0f, 2.0f);
        REQUIRE(point.GetX() == Approx(1.0f));
        REQUIRE(point.GetY() == Approx(2.0f));
    }

    SECTION("Setters") {
        inspirecv::Point2f point(1.0f, 1.0f);
        point.SetX(5.0f);
        point.SetY(6.0f);
        REQUIRE(point.GetX() == Approx(5.0f));
        REQUIRE(point.GetY() == Approx(6.0f));
    }

    SECTION("Length") {
        inspirecv::Point2f point(3.0f, 4.0f);
        REQUIRE(point.Length() == Approx(5.0f));
    }

    SECTION("Distance") {
        inspirecv::Point2f origin(0.0f, 0.0f);
        inspirecv::Point2f target(3.0f, 4.0f);
        REQUIRE(origin.Distance(target) == Approx(5.0f));
    }

    SECTION("Dot Product") {
        inspirecv::Point2f vec1(2.0f, 3.0f);
        inspirecv::Point2f vec2(4.0f, 5.0f);
        REQUIRE(vec1.Dot(vec2) == Approx(23.0f));
    }

    SECTION("Cross Product") {
        inspirecv::Point2f vec1(1.0f, 2.0f);
        inspirecv::Point2f vec2(3.0f, 4.0f);
        REQUIRE(vec1.Cross(vec2) == Approx(-2.0f));
    }

    SECTION("Type Conversion") {
        inspirecv::Point2f float_point(1.5f, 2.5f);
        auto int_point = float_point.As<int>();
        REQUIRE(int_point.GetX() == 1);
        REQUIRE(int_point.GetY() == 2);
    }

    SECTION("Move Semantics") {
        inspirecv::Point2f source(7.0f, 8.0f);
        inspirecv::Point2f target = std::move(source);
        REQUIRE(target.GetX() == Approx(7.0f));
        REQUIRE(target.GetY() == Approx(8.0f));
    }

    SECTION("Different Types") {
        inspirecv::Point2i int_point(1, 2);
        inspirecv::Point2d double_point(1.23, 4.56);
        REQUIRE(int_point.GetX() == 1);
        REQUIRE(double_point.GetX() == Approx(1.23));
    }

    SECTION("Type Conversions Between Types") {
        SECTION("Float to Double") {
            inspirecv::Point2f float_point(1.5f, 2.5f);
            auto double_point = float_point.As<double>();
            REQUIRE(double_point.GetX() == Approx(1.5));
            REQUIRE(double_point.GetY() == Approx(2.5));
        }

        SECTION("Double to Float") {
            inspirecv::Point2d double_point(3.7, 4.2);
            auto float_point = double_point.As<float>();
            REQUIRE(float_point.GetX() == Approx(3.7f));
            REQUIRE(float_point.GetY() == Approx(4.2f));
        }

        SECTION("Double to Int") {
            inspirecv::Point2d double_point(3.7, 4.2);
            auto int_point = double_point.As<int>();
            REQUIRE(int_point.GetX() == 3);
            REQUIRE(int_point.GetY() == 4);
        }

        SECTION("Int to Float and Double") {
            inspirecv::Point2i int_point(5, 6);

            auto float_point = int_point.As<float>();
            REQUIRE(float_point.GetX() == Approx(5.0f));
            REQUIRE(float_point.GetY() == Approx(6.0f));

            auto double_point = int_point.As<double>();
            REQUIRE(double_point.GetX() == Approx(5.0));
            REQUIRE(double_point.GetY() == Approx(6.0));
        }
    }

    SECTION("apply transform") {
        // invariant
        auto identity_transform = inspirecv::TransformMatrix::Identity();
        std::vector<inspirecv::Point2f> points = {{0, 0}, {1, 1}, {2, 2}};
        auto transformed_points = inspirecv::ApplyTransformToPoints(points, identity_transform);
        for (size_t i = 0; i < points.size(); ++i) {
            REQUIRE(transformed_points[i] == points[i]);
        }

        // rot 90
        auto rot90_transform = inspirecv::TransformMatrix::Create(0, -1, 0, 1, 0, 0);
        auto rotated_points = inspirecv::ApplyTransformToPoints(points, rot90_transform);
        REQUIRE(rotated_points[0] == inspirecv::Point2f(0, 0));
        REQUIRE(rotated_points[1] == inspirecv::Point2f(-1, 1));
        REQUIRE(rotated_points[2] == inspirecv::Point2f(-2, 2));

        // rot 90 and scale
        inspirecv::Size2f image_size(1920, 1080);
        inspirecv::TransformMatrix transform_on_image =
          inspirecv::TransformMatrix::Create(0, -0.5, 0, 0.5, 0, 0);
        std::vector<inspirecv::Point2f> points_on_image = {{0, 0}, {1920, 0}, {0, 1080}};
        auto rotated_points_on_image =
          inspirecv::ApplyTransformToPoints(points_on_image, transform_on_image);
        REQUIRE(rotated_points_on_image[0] == inspirecv::Point2f(0, 0));
        REQUIRE(rotated_points_on_image[1] == inspirecv::Point2f(0, 960));
        REQUIRE(rotated_points_on_image[2] == inspirecv::Point2f(-540, 0));
    }
}

TEST_CASE("test_rects", "[geometry]") {
    DRAW_SPLIT_LINE;

    SECTION("Default Constructor") {
        inspirecv::Rect2f r1;
        REQUIRE(r1.GetX() == Approx(0.0f));
        REQUIRE(r1.GetY() == Approx(0.0f));
        REQUIRE(r1.GetWidth() == Approx(0.0f));
        REQUIRE(r1.GetHeight() == Approx(0.0f));
    }

    SECTION("Parameterized Constructor") {
        inspirecv::Rect2f r2(10.0f, 20.0f, 30.0f, 40.0f);
        REQUIRE(r2.GetX() == Approx(10.0f));
        REQUIRE(r2.GetY() == Approx(20.0f));
        REQUIRE(r2.GetWidth() == Approx(30.0f));
        REQUIRE(r2.GetHeight() == Approx(40.0f));
    }

    SECTION("Copy Constructor and Assignment") {
        inspirecv::Rect2f r2(10.0f, 20.0f, 30.0f, 40.0f);

        SECTION("Copy Constructor") {
            inspirecv::Rect2f r3 = r2;
            REQUIRE(r3.GetX() == r2.GetX());
            REQUIRE(r3.GetY() == r2.GetY());
            REQUIRE(r3.GetWidth() == r2.GetWidth());
            REQUIRE(r3.GetHeight() == r2.GetHeight());
        }

        SECTION("Assignment Operator") {
            inspirecv::Rect2f r4;
            r4 = r2;
            REQUIRE(r4.GetX() == r2.GetX());
            REQUIRE(r4.GetY() == r2.GetY());
            REQUIRE(r4.GetWidth() == r2.GetWidth());
            REQUIRE(r4.GetHeight() == r2.GetHeight());
        }
    }

    SECTION("Setters") {
        inspirecv::Rect2f r2(10.0f, 20.0f, 30.0f, 40.0f);
        r2.SetX(15.0f);
        r2.SetY(25.0f);
        r2.SetWidth(35.0f);
        r2.SetHeight(45.0f);
        REQUIRE(r2.GetX() == Approx(15.0f));
        REQUIRE(r2.GetY() == Approx(25.0f));
        REQUIRE(r2.GetWidth() == Approx(35.0f));
        REQUIRE(r2.GetHeight() == Approx(45.0f));
    }

    SECTION("Boundary Points") {
        inspirecv::Rect2f r5(10.0f, 20.0f, 30.0f, 40.0f);

        SECTION("TopLeft") {
            auto topLeft = r5.TopLeft();
            REQUIRE(topLeft.GetX() == Approx(10.0f));
            REQUIRE(topLeft.GetY() == Approx(20.0f));
        }

        SECTION("TopRight") {
            auto topRight = r5.TopRight();
            REQUIRE(topRight.GetX() == Approx(40.0f));
            REQUIRE(topRight.GetY() == Approx(20.0f));
        }

        SECTION("BottomLeft") {
            auto bottomLeft = r5.BottomLeft();
            REQUIRE(bottomLeft.GetX() == Approx(10.0f));
            REQUIRE(bottomLeft.GetY() == Approx(60.0f));
        }

        SECTION("BottomRight") {
            auto bottomRight = r5.BottomRight();
            REQUIRE(bottomRight.GetX() == Approx(40.0f));
            REQUIRE(bottomRight.GetY() == Approx(60.0f));
        }

        SECTION("Center") {
            auto center = r5.Center();
            REQUIRE(center.GetX() == Approx(25.0f));
            REQUIRE(center.GetY() == Approx(40.0f));
        }
    }

    SECTION("Four Vertices") {
        inspirecv::Rect2f r5(10.0f, 20.0f, 30.0f, 40.0f);
        auto vertices = r5.ToFourVertices();
        REQUIRE(vertices.size() == 4);
        REQUIRE(vertices[0].GetX() == Approx(10.0f));
        REQUIRE(vertices[0].GetY() == Approx(20.0f));
        REQUIRE(vertices[1].GetX() == Approx(40.0f));
        REQUIRE(vertices[1].GetY() == Approx(20.0f));
        REQUIRE(vertices[2].GetX() == Approx(40.0f));
        REQUIRE(vertices[2].GetY() == Approx(60.0f));
        REQUIRE(vertices[3].GetX() == Approx(10.0f));
        REQUIRE(vertices[3].GetY() == Approx(60.0f));
    }

    SECTION("Area and Empty") {
        SECTION("Non-Empty Rectangle") {
            inspirecv::Rect2f r5(10.0f, 20.0f, 30.0f, 40.0f);
            REQUIRE(r5.Area() == Approx(1200.0f));
            REQUIRE_FALSE(r5.Empty());
        }

        SECTION("Empty Rectangle") {
            inspirecv::Rect2f r6(10.0f, 20.0f, 0.0f, 40.0f);
            REQUIRE(r6.Empty());
        }
    }

    SECTION("Contains") {
        inspirecv::Rect2f r5(10.0f, 20.0f, 30.0f, 40.0f);

        SECTION("Contains Point") {
            inspirecv::Point2f p1(15.0f, 25.0f);  // Inside
            inspirecv::Point2f p2(5.0f, 25.0f);   // Outside
            REQUIRE(r5.Contains(p1));
            REQUIRE_FALSE(r5.Contains(p2));
        }

        SECTION("Contains Rect") {
            inspirecv::Rect2f r7(15.0f, 25.0f, 10.0f, 20.0f);  // Inside
            inspirecv::Rect2f r8(5.0f, 25.0f, 10.0f, 20.0f);   // Partially outside
            REQUIRE(r5.Contains(r7));
            REQUIRE_FALSE(r5.Contains(r8));
        }
    }

    SECTION("Intersection and Union") {
        inspirecv::Rect2f r9(0.0f, 0.0f, 20.0f, 30.0f);
        inspirecv::Rect2f r10(10.0f, 20.0f, 30.0f, 40.0f);

        SECTION("Intersection") {
            auto intersection = r9.Intersect(r10);
            REQUIRE(intersection.GetX() == Approx(10.0f));
            REQUIRE(intersection.GetY() == Approx(20.0f));
            REQUIRE(intersection.GetWidth() == Approx(10.0f));
            REQUIRE(intersection.GetHeight() == Approx(10.0f));
        }

        SECTION("Union") {
            auto union_rect = r9.Union(r10);
            REQUIRE(union_rect.GetX() == Approx(0.0f));
            REQUIRE(union_rect.GetY() == Approx(0.0f));
            REQUIRE(union_rect.GetWidth() == Approx(40.0f));
            REQUIRE(union_rect.GetHeight() == Approx(60.0f));
        }

        SECTION("IoU") {
            REQUIRE(r9.IoU(r10) == Approx(100.0f / 1700.0f));
        }
    }

    SECTION("Transformations") {
        SECTION("Scale") {
            inspirecv::Rect2f r11(10.0f, 20.0f, 30.0f, 40.0f);
            r11.Scale(2.0f, 3.0f);
            REQUIRE(r11.GetWidth() == Approx(60.0f));
            REQUIRE(r11.GetHeight() == Approx(120.0f));
        }

        SECTION("Translate") {
            inspirecv::Rect2f r12(10.0f, 20.0f, 30.0f, 40.0f);
            r12.Translate(5.0f, 10.0f);
            REQUIRE(r12.GetX() == Approx(15.0f));
            REQUIRE(r12.GetY() == Approx(30.0f));
        }
    }

    SECTION("Square Conversion") {
        inspirecv::Rect2f r13(10.0f, 20.0f, 30.0f, 40.0f);
        auto square = r13.Square(1.2f);  // Convert to square with 1.2x scaling
        REQUIRE(square.GetWidth() == square.GetHeight());
        REQUIRE(square.GetWidth() == Approx(48.0f));  // max(30, 40) * 1.2 = 48
        REQUIRE(square.Center() == r13.Center());     // Center should remain unchanged
    }

    SECTION("Type Conversions") {
        SECTION("Float to Int") {
            inspirecv::Rect2f r14(1.5f, 2.5f, 3.5f, 4.5f);
            auto r14_int = r14.As<int>();
            REQUIRE(r14_int.GetX() == 1);
            REQUIRE(r14_int.GetY() == 2);
            REQUIRE(r14_int.GetWidth() == 3);
            REQUIRE(r14_int.GetHeight() == 4);
        }

        SECTION("Different Types") {
            inspirecv::Rect2i ri1(1, 2, 3, 4);
            inspirecv::Rect2d rd1(1.23, 4.56, 7.89, 10.11);
            REQUIRE(ri1.GetX() == 1);
            REQUIRE(rd1.GetX() == Approx(1.23));
        }
    }

    SECTION("Safe Rect") {
        inspirecv::Rect2f r15(90.0f, 90.0f, 30.0f, 40.0f);
        auto safe_rect = r15.SafeRect(100.0f, 100.0f);
        REQUIRE(safe_rect.GetX() + safe_rect.GetWidth() <= 100.0f);
        REQUIRE(safe_rect.GetY() + safe_rect.GetHeight() <= 100.0f);
    }

    SECTION("Static Create Methods") {
        SECTION("Create from Coordinates") {
            auto r16 = inspirecv::Rect2f::Create(10.0f, 20.0f, 30.0f, 40.0f);
            REQUIRE(r16.GetX() == Approx(10.0f));
            REQUIRE(r16.GetY() == Approx(20.0f));
            REQUIRE(r16.GetWidth() == Approx(30.0f));
            REQUIRE(r16.GetHeight() == Approx(40.0f));
        }

        SECTION("Create from Points") {
            auto r17 = inspirecv::Rect2f::Create(inspirecv::Point2f(10.0f, 20.0f),
                                                 inspirecv::Point2f(40.0f, 60.0f));
            REQUIRE(r17.GetX() == Approx(10.0f));
            REQUIRE(r17.GetY() == Approx(20.0f));
            REQUIRE(r17.GetWidth() == Approx(30.0f));
            REQUIRE(r17.GetHeight() == Approx(40.0f));
        }
    }

    SECTION("Type Conversions Between All Types") {
        SECTION("Int to Float and Double") {
            inspirecv::Rect2i ri2(10, 20, 30, 40);
            auto ri2_float = ri2.As<float>();
            auto ri2_double = ri2.As<double>();
            REQUIRE(ri2_float.GetX() == Approx(10.0f));
            REQUIRE(ri2_float.GetY() == Approx(20.0f));
            REQUIRE(ri2_float.GetWidth() == Approx(30.0f));
            REQUIRE(ri2_float.GetHeight() == Approx(40.0f));
            REQUIRE(ri2_double.GetX() == Approx(10.0));
            REQUIRE(ri2_double.GetY() == Approx(20.0));
            REQUIRE(ri2_double.GetWidth() == Approx(30.0));
            REQUIRE(ri2_double.GetHeight() == Approx(40.0));
        }

        SECTION("Float to Int and Double") {
            inspirecv::Rect2f rf2(10.5f, 20.5f, 30.5f, 40.5f);
            auto rf2_int = rf2.As<int>();
            auto rf2_double = rf2.As<double>();
            REQUIRE(rf2_int.GetX() == 10);
            REQUIRE(rf2_int.GetY() == 20);
            REQUIRE(rf2_int.GetWidth() == 30);
            REQUIRE(rf2_int.GetHeight() == 40);
            REQUIRE(rf2_double.GetX() == Approx(10.5));
            REQUIRE(rf2_double.GetY() == Approx(20.5));
            REQUIRE(rf2_double.GetWidth() == Approx(30.5));
            REQUIRE(rf2_double.GetHeight() == Approx(40.5));
        }

        SECTION("Double to Int and Float") {
            inspirecv::Rect2d rd2(10.5, 20.5, 30.5, 40.5);
            auto rd2_int = rd2.As<int>();
            auto rd2_float = rd2.As<float>();
            REQUIRE(rd2_int.GetX() == 10);
            REQUIRE(rd2_int.GetY() == 20);
            REQUIRE(rd2_int.GetWidth() == 30);
            REQUIRE(rd2_int.GetHeight() == 40);
            REQUIRE(rd2_float.GetX() == Approx(10.5f));
            REQUIRE(rd2_float.GetY() == Approx(20.5f));
            REQUIRE(rd2_float.GetWidth() == Approx(30.5f));
            REQUIRE(rd2_float.GetHeight() == Approx(40.5f));
        }
    }

    SECTION("apply transform to rect") {
        inspirecv::Rect2f rect(0, 0, 1920, 1080);
        inspirecv::TransformMatrix transform_on_image =
          inspirecv::TransformMatrix::Create(0, -0.5, 0, 0.5, 0, 0);
        auto transformed_rect = inspirecv::ApplyTransformToRect(rect, transform_on_image);
        REQUIRE(transformed_rect.GetX() == Approx(-540));
        REQUIRE(transformed_rect.GetY() == Approx(0));
        REQUIRE(transformed_rect.GetWidth() == Approx(540));
        REQUIRE(transformed_rect.GetHeight() == Approx(960));
    }
}

TEST_CASE("test_sizes", "[geometry]") {
    DRAW_SPLIT_LINE;
    // Test default constructor
    inspirecv::Size2f s1;
    REQUIRE(s1.GetWidth() == Approx(0.0f));
    REQUIRE(s1.GetHeight() == Approx(0.0f));

    // Test parameterized constructor
    inspirecv::Size2f s2(30.5f, 40.5f);
    REQUIRE(s2.GetWidth() == Approx(30.5f));
    REQUIRE(s2.GetHeight() == Approx(40.5f));

    // Test Create factory method
    auto s3 = inspirecv::Size2f::Create(50.0f, 60.0f);
    REQUIRE(s3.GetWidth() == Approx(50.0f));
    REQUIRE(s3.GetHeight() == Approx(60.0f));

    // Test setters
    s3.SetWidth(70.0f);
    s3.SetHeight(80.0f);
    REQUIRE(s3.GetWidth() == Approx(70.0f));
    REQUIRE(s3.GetHeight() == Approx(80.0f));

    // Test Area()
    inspirecv::Size2f s4(10.0f, 20.0f);
    REQUIRE(s4.Area() == Approx(200.0f));

    // Test Empty()
    inspirecv::Size2f s5(0.0f, 10.0f);
    inspirecv::Size2f s6(10.0f, 0.0f);
    inspirecv::Size2f s7(0.0f, 0.0f);
    REQUIRE(s5.Empty() == true);
    REQUIRE(s6.Empty() == true);
    REQUIRE(s7.Empty() == true);
    REQUIRE(s4.Empty() == false);

    // Test Scale()
    inspirecv::Size2f s8(10.0f, 20.0f);
    s8.Scale(2.0f, 3.0f);
    REQUIRE(s8.GetWidth() == Approx(20.0f));
    REQUIRE(s8.GetHeight() == Approx(60.0f));

    // Test copy constructor and assignment
    inspirecv::Size2f s9(15.0f, 25.0f);
    inspirecv::Size2f s10(s9);
    REQUIRE(s10.GetWidth() == Approx(15.0f));
    REQUIRE(s10.GetHeight() == Approx(25.0f));

    inspirecv::Size2f s11;
    s11 = s9;
    REQUIRE(s11.GetWidth() == Approx(15.0f));
    REQUIRE(s11.GetHeight() == Approx(25.0f));
}

TEST_CASE("test_transform_matrix", "[geometry]") {
    DRAW_SPLIT_LINE;

    SECTION("Default Constructor") {
        inspirecv::TransformMatrix m1;
        REQUIRE(m1.IsIdentity() == true);
    }

    SECTION("Parameterized Constructor") {
        inspirecv::TransformMatrix m2(2.0f, 0.0f, 10.0f, 0.0f, 2.0f, 20.0f);
        REQUIRE(m2.Get(0, 0) == Approx(2.0f));
        REQUIRE(m2.Get(0, 1) == Approx(0.0f));
        REQUIRE(m2.Get(0, 2) == Approx(10.0f));
        REQUIRE(m2.Get(1, 0) == Approx(0.0f));
        REQUIRE(m2.Get(1, 1) == Approx(2.0f));
        REQUIRE(m2.Get(1, 2) == Approx(20.0f));
    }

    SECTION("Factory Method") {
        auto m3 = inspirecv::TransformMatrix::Create(1.0f, 0.0f, 5.0f, 0.0f, 1.0f, 10.0f);
        REQUIRE(m3.Get(0, 0) == Approx(1.0f));
        REQUIRE(m3.Get(0, 2) == Approx(5.0f));
        REQUIRE(m3.Get(1, 2) == Approx(10.0f));
    }

    SECTION("Array Access") {
        inspirecv::TransformMatrix m2(2.0f, 0.0f, 10.0f, 0.0f, 2.0f, 20.0f);
        REQUIRE(m2[0] == Approx(2.0f));
        REQUIRE(m2[2] == Approx(10.0f));
        REQUIRE(m2[5] == Approx(20.0f));
    }

    SECTION("Squeeze") {
        inspirecv::TransformMatrix m2(2.0f, 0.0f, 10.0f, 0.0f, 2.0f, 20.0f);
        auto vec = m2.Squeeze();
        REQUIRE(vec[0] == Approx(2.0f));
        REQUIRE(vec[2] == Approx(10.0f));
        REQUIRE(vec[5] == Approx(20.0f));
    }

    SECTION("Identity") {
        inspirecv::TransformMatrix m4;
        m4.SetIdentity();
        REQUIRE(m4.IsIdentity() == true);
    }

    SECTION("Transform Operations") {
        SECTION("Translation") {
            inspirecv::TransformMatrix m5;
            m5.Translate(10.0f, 20.0f);
            REQUIRE(m5.Get(0, 2) == Approx(10.0f));
            REQUIRE(m5.Get(1, 2) == Approx(20.0f));
        }

        SECTION("Scaling") {
            inspirecv::TransformMatrix m6;
            m6.Scale(2.0f, 3.0f);
            REQUIRE(m6.Get(0, 0) == Approx(2.0f));
            REQUIRE(m6.Get(1, 1) == Approx(3.0f));
        }

        SECTION("Rotation") {
            inspirecv::TransformMatrix m7;
            m7.Rotate(90.0f);
            REQUIRE(m7.Get(0, 0) == Approx(0.0f).margin(0.0001f));
            REQUIRE(m7.Get(0, 1) == Approx(-1.0f).margin(0.0001f));
            REQUIRE(m7.Get(1, 0) == Approx(1.0f).margin(0.0001f));
            REQUIRE(m7.Get(1, 1) == Approx(0.0f).margin(0.0001f));
        }
    }

    SECTION("Clone") {
        inspirecv::TransformMatrix m2(2.0f, 0.0f, 10.0f, 0.0f, 2.0f, 20.0f);
        auto m8 = m2.Clone();
        REQUIRE(m8.Get(0, 0) == m2.Get(0, 0));
        REQUIRE(m8.Get(1, 2) == m2.Get(1, 2));
    }

    SECTION("Matrix Multiplication") {
        inspirecv::TransformMatrix m9(2.0f, 0.0f, 10.0f, 0.0f, 2.0f, 20.0f);
        inspirecv::TransformMatrix m10(3.0f, 0.0f, 30.0f, 0.0f, 3.0f, 40.0f);
        auto m11 = m9.Multiply(m10);
        REQUIRE(m11.Get(0, 0) == Approx(6.0f));
        REQUIRE(m11.Get(0, 2) == Approx(70.0f));
        REQUIRE(m11.Get(1, 1) == Approx(6.0f));
        REQUIRE(m11.Get(1, 2) == Approx(100.0f));
    }

    SECTION("Matrix Inversion") {
        inspirecv::TransformMatrix m12(2.0f, 0.0f, 10.0f, 0.0f, 2.0f, 20.0f);
        auto m13 = m12.GetInverse();
        REQUIRE(m13.Get(0, 0) == Approx(0.5f));
        REQUIRE(m13.Get(0, 2) == Approx(-5.0f));
        REQUIRE(m13.Get(1, 1) == Approx(0.5f));
        REQUIRE(m13.Get(1, 2) == Approx(-10.0f));
    }

    SECTION("Copy Constructor and Assignment") {
        inspirecv::TransformMatrix m12(2.0f, 0.0f, 10.0f, 0.0f, 2.0f, 20.0f);

        SECTION("Copy Constructor") {
            inspirecv::TransformMatrix m14(m12);
            REQUIRE(m14.Get(0, 0) == m12.Get(0, 0));
            REQUIRE(m14.Get(1, 2) == m12.Get(1, 2));
        }

        SECTION("Assignment Operator") {
            inspirecv::TransformMatrix m15;
            m15 = m12;
            REQUIRE(m15.Get(0, 0) == m12.Get(0, 0));
            REQUIRE(m15.Get(1, 2) == m12.Get(1, 2));
        }
    }

    SECTION("similarity transform estimate") {
        auto identity_transform = inspirecv::TransformMatrix::Identity();
        // no transform
        std::vector<inspirecv::Point2f> src_points = {{0, 0}, {50, 0}, {0, 50}, {50, 50}};
        std::vector<inspirecv::Point2f> dst_points = {{0, 0}, {50, 0}, {0, 50}, {50, 50}};
        auto transform = inspirecv::SimilarityTransformEstimate(src_points, dst_points);
        REQUIRE_NEAR_ARRAY(transform.Squeeze(), identity_transform.Squeeze(), 0.0001f);

        // 90 degree rotation
        std::vector<inspirecv::Point2f> src_points_90 = {{0, 0}, {90, 0}, {0, 90}};
        std::vector<inspirecv::Point2f> dst_points_90 = {{0, 0}, {0, 90}, {-90, 0}};
        auto transform_90 = inspirecv::SimilarityTransformEstimate(src_points_90, dst_points_90);
        auto rotate_90 = inspirecv::TransformMatrix::Rotate90();
        REQUIRE_NEAR_ARRAY(transform_90.Squeeze(), rotate_90.Squeeze(), 0.0001f);

        // scale
        std::vector<inspirecv::Point2f> src_points_scale = {{0, 0}, {10, 0}, {0, 10}};
        std::vector<inspirecv::Point2f> dst_points_scale = {{0, 0}, {20, 0}, {0, 20}};
        auto transform_scale =
          inspirecv::SimilarityTransformEstimate(src_points_scale, dst_points_scale);
        auto scale = inspirecv::TransformMatrix::Create(2.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f);
        REQUIRE_NEAR_ARRAY(transform_scale.Squeeze(), scale.Squeeze(), 0.0001f);

        // translate
        std::vector<inspirecv::Point2f> src_points_translate = {{0, 0}, {10, 0}, {0, 10}};
        std::vector<inspirecv::Point2f> dst_points_translate = {{10, 20}, {20, 20}, {10, 30}};
        auto transform_translate =
          inspirecv::SimilarityTransformEstimate(src_points_translate, dst_points_translate);
        auto translate = inspirecv::TransformMatrix::Create(1.0f, 0.0f, 10.0f, 0.0f, 1.0f, 20.0f);
        REQUIRE_NEAR_ARRAY(transform_translate.Squeeze(), translate.Squeeze(), 0.0001f);

        // rotate + translate
        std::vector<inspirecv::Point2f> src_points_rotate_translate = {{0, 0}, {10, 0}, {0, 10}};
        std::vector<inspirecv::Point2f> dst_points_rotate_translate = {
          {10, 20}, {20, 20}, {10, 30}};
        auto transform_rotate_translate = inspirecv::SimilarityTransformEstimate(
          src_points_rotate_translate, dst_points_rotate_translate);
        auto translate_rotate =
          inspirecv::TransformMatrix::Create(1.0f, 0.0f, 10.0f, 0.0f, 1.0f, 20.0f);
        REQUIRE_NEAR_ARRAY(transform_rotate_translate.Squeeze(), translate_rotate.Squeeze(),
                           0.0001f);
    }
}
