#ifndef INSPIRECV_IMPL_IMPL_H
#define INSPIRECV_IMPL_IMPL_H

#include "logging.h"
#include "check.h"

#ifdef INSPIRECV_BACKEND_OPENCV
#define INSPIRECV_BACKEND_TAG "OpenCV"
#include "opencv/all.h"
#else
#define INSPIRECV_BACKEND_TAG "OKCV"
#include "okcv/all.h"
#endif

#endif  // INSPIRECV_IMPL_IMPL_H