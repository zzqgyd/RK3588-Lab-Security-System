#ifndef INSPIRECV_CORE_COLOR_H
#define INSPIRECV_CORE_COLOR_H

#include <vector>

namespace inspirecv {

namespace Color {

const std::vector<double> Red = {0.0, 0.0, 255.0};
const std::vector<double> Green = {0.0, 255.0, 0.0};
const std::vector<double> Blue = {255.0, 0.0, 0.0};
const std::vector<double> Black = {0.0, 0.0, 0.0};
const std::vector<double> White = {255.0, 255.0, 255.0};
const std::vector<double> Yellow = {0.0, 255.0, 255.0};
const std::vector<double> Magenta = {255.0, 0.0, 255.0};
const std::vector<double> Cyan = {255.0, 255.0, 0.0};
const std::vector<double> Gray = {128.0, 128.0, 128.0};
const std::vector<double> Orange = {0.0, 128.0, 255.0};
const std::vector<double> Purple = {128.0, 0.0, 128.0};
const std::vector<double> Brown = {42.0, 42.0, 165.0};
const std::vector<double> Pink = {147.0, 20.0, 255.0};

}  // namespace Color

}  // namespace inspirecv

#endif  // INSPIRECV_CORE_COLOR_H
