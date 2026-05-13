#pragma once

#include "inspirecv/version.h"

namespace inspirecv {

const char* GetVersion() {
    return "InspireCV v0.6.0@OpenCV - Build Time: 2026-04-27";
}

const char* GetCVBackend() {
    return "OpenCV";
}

}  // namespace inspirecv
