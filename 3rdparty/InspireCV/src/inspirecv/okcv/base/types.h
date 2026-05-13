#ifndef OKCV_TYPES_H_
#define OKCV_TYPES_H_

#include <cstdint>
#include <string>

#ifndef OKCV_API
#define OKCV_API
#endif

namespace okcv {

/**
 * @brief Enumeration for different data types.
 */
typedef enum DataType {
    DT_NO_TYPE = 0,
    DT_FLOAT = 1,
    DT_DOUBLE = 2,
    DT_INT64 = 3,
    DT_INT32 = 4,
    DT_INT16 = 5,
    DT_INT8 = 6,
    DT_UINT64 = 7,
    DT_UINT32 = 8,
    DT_UINT16 = 9,
    DT_UINT8 = 10,
    DT_BOOL = 11,
} DataType;

/**
 * @brief Validates type T for whether it is a supported DataType.
 */
template <class T>
struct IsValidDataType;

/**
 * @brief Converts a type T to its corresponding DataType constant.
 */
template <class T>
struct DataTypeToEnum {
    static_assert(IsValidDataType<T>::value, "Specified Data Type not supported");
};  // Specializations below

/**
 * @brief Converts a DataType constant to its corresponding type.
 */
template <DataType VALUE>
struct EnumToDataType {};  // Specializations below

/**
 * @brief Template specialization for both DataTypeToEnum and EnumToDataType.
 */
#define MATCH_TYPE_AND_ENUM(TYPE, ENUM)         \
    template <>                                 \
    struct DataTypeToEnum<TYPE> {               \
        static DataType v() {                   \
            return ENUM;                        \
        }                                       \
        static constexpr DataType value = ENUM; \
    };                                          \
    template <>                                 \
    struct IsValidDataType<TYPE> {              \
        static constexpr bool value = true;     \
    };                                          \
    template <>                                 \
    struct EnumToDataType<ENUM> {               \
        typedef TYPE Type;                      \
    }

MATCH_TYPE_AND_ENUM(float, DT_FLOAT);
MATCH_TYPE_AND_ENUM(double, DT_DOUBLE);
MATCH_TYPE_AND_ENUM(int64_t, DT_INT64);
MATCH_TYPE_AND_ENUM(int32_t, DT_INT32);
MATCH_TYPE_AND_ENUM(int16_t, DT_INT16);
MATCH_TYPE_AND_ENUM(int8_t, DT_INT8);
MATCH_TYPE_AND_ENUM(uint64_t, DT_UINT64);
MATCH_TYPE_AND_ENUM(uint32_t, DT_UINT32);
MATCH_TYPE_AND_ENUM(uint16_t, DT_UINT16);
MATCH_TYPE_AND_ENUM(uint8_t, DT_UINT8);
MATCH_TYPE_AND_ENUM(bool, DT_BOOL);

#undef MATCH_TYPE_AND_ENUM

/**
 * @brief All types not specialized are marked invalid.
 */
template <class T>
struct IsValidDataType {
    static constexpr bool value = false;
};

/**
 * @brief Returns the number of bytes for a given DataType.
 */
inline int Bytes(DataType dtype) {
    switch (dtype) {
        case DT_NO_TYPE:
            return 0;
        case DT_FLOAT:
            return sizeof(EnumToDataType<DT_FLOAT>::Type);
        case DT_DOUBLE:
            return sizeof(EnumToDataType<DT_DOUBLE>::Type);
        case DT_INT64:
            return sizeof(EnumToDataType<DT_INT64>::Type);
        case DT_INT32:
            return sizeof(EnumToDataType<DT_INT32>::Type);
        case DT_INT16:
            return sizeof(EnumToDataType<DT_INT16>::Type);
        case DT_INT8:
            return sizeof(EnumToDataType<DT_INT8>::Type);
        case DT_UINT64:
            return sizeof(EnumToDataType<DT_UINT64>::Type);
        case DT_UINT32:
            return sizeof(EnumToDataType<DT_UINT32>::Type);
        case DT_UINT16:
            return sizeof(EnumToDataType<DT_UINT16>::Type);
        case DT_UINT8:
            return sizeof(EnumToDataType<DT_UINT8>::Type);
        case DT_BOOL:
            return sizeof(EnumToDataType<DT_BOOL>::Type);
    }
    return 0;
}

/**
 * @brief Converts a string to its corresponding DataType constant.
 */
DataType StringToDataType(const std::string &s);

/**
 * @brief Converts a DataType constant to its corresponding string.
 */
std::string DataTypeToString(DataType dtype);

}  // namespace okcv

#endif  // OKCV_TYPES_H_
