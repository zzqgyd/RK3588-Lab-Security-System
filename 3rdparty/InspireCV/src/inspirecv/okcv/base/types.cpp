#include <string>

#include "logging.h"
#include "types.h"

namespace okcv {

DataType StringToDataType(const std::string &s) {
    if (s == "no_type")
        return DT_NO_TYPE;
    else if (s == "float")
        return DT_FLOAT;
    else if (s == "double")
        return DT_DOUBLE;
    else if (s == "int64")
        return DT_INT64;
    else if (s == "int32")
        return DT_INT32;
    else if (s == "int16")
        return DT_INT16;
    else if (s == "int8")
        return DT_INT8;
    else if (s == "uint64")
        return DT_UINT64;
    else if (s == "uint32")
        return DT_UINT32;
    else if (s == "uint16")
        return DT_UINT16;
    else if (s == "uint8")
        return DT_UINT8;
    else if (s == "bool")
        return DT_BOOL;
    else
        INSPIRECV_LOG(FATAL) << "data type string error: " << s;
    return DT_NO_TYPE;
}

std::string DataTypeToString(DataType dtype) {
    switch (dtype) {
        case DT_NO_TYPE:
            return "NO_TYPE";
        case DT_FLOAT:
            return "FLOAT";
        case DT_DOUBLE:
            return "DOUBLE";
        case DT_INT64:
            return "INT64";
        case DT_INT32:
            return "INT32";
        case DT_INT16:
            return "INT16";
        case DT_INT8:
            return "INT8";
        case DT_UINT64:
            return "UINT64";
        case DT_UINT32:
            return "UINT32";
        case DT_UINT16:
            return "UINT16";
        case DT_UINT8:
            return "UINT8";
        case DT_BOOL:
            return "BOOL";
    }
    return 0;
}

}  // namespace okcv
