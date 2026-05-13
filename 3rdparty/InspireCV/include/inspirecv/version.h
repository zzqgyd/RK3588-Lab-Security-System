#ifndef INSPIRECV_VERSION_H
#define INSPIRECV_VERSION_H

#ifndef INSPIRECV_API
#define INSPIRECV_API
#endif

namespace inspirecv {

const char* INSPIRECV_API GetVersion();

const char* INSPIRECV_API GetCVBackend();

}  // namespace inspirecv

#endif  // INSPIRECV_VERSION_H
