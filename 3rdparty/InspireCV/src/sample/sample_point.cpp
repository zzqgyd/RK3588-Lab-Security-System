
#include "inspirecv/inspirecv.h"

#include <iostream>

using namespace inspirecv;
int main() {
    Point<int> p1(1, 2);
    Point<int> p2(3, 4);
    std::cout << p1.Distance(p2) << std::endl;

    Rect<int> r1(0, 0, 10, 10);
    Rect<int> r2(5, 5, 15, 15);
    std::cout << r1.IoU(r2) << std::endl;
    r1.Scale(2, 2);
    std::cout << r1.IoU(r2) << std::endl;

    std::cout << p1 << std::endl;

    std::cout << r1 << std::endl;

    Size<int> s1(10, 20);
    std::cout << s1 << std::endl;

    TransformMatrix m1 = TransformMatrix::Create(1, 0, 0, 0, 1, 0);
    std::cout << m1 << std::endl;

    std::vector<Point2i> points = {{1, 2}, {3, 4}, {5, 6}};

    std::cout << points << std::endl;

    return 0;
}