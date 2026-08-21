#include <cstdint>
#include <cstdlib>

extern "C" {

struct FfiRecord {
    std::uint32_t id;
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
};

struct FfiOpaque {
    std::int32_t value;
};

struct FfiSmall {
    std::int32_t left;
    std::int32_t right;
};

struct FfiMixed {
    double precise;
    float fractional;
    std::int32_t integer;
};

FfiRecord ffi_make_record(std::int32_t seed) {
    return {
        static_cast<std::uint32_t>(seed),
        seed + 1,
        seed + 2,
        seed + 3,
        seed + 4
    };
}

FfiSmall ffi_make_small(std::int32_t seed) {
    return {seed, seed + 2};
}

std::int32_t ffi_sum_small(FfiSmall value) {
    return value.left + value.right;
}

FfiMixed ffi_make_mixed() {
    return {1.5, 2.25f, 3};
}

double ffi_sum_mixed(FfiMixed value) {
    return value.precise + value.fractional + value.integer;
}

std::int64_t ffi_sum_record(FfiRecord value) {
    return static_cast<std::int64_t>(value.id) + value.x + value.y +
           value.width + value.height;
}

void ffi_increment_record(FfiRecord* value, std::int32_t amount) {
    value->x += amount;
    value->y += amount;
}

FfiRecord* ffi_echo_record(FfiRecord* value) {
    return value;
}

FfiOpaque* ffi_opaque_create(std::int32_t value) {
    auto* result = static_cast<FfiOpaque*>(std::malloc(sizeof(FfiOpaque)));
    result->value = value;
    return result;
}

std::int32_t ffi_opaque_value(FfiOpaque* value) {
    return value->value;
}

void ffi_opaque_destroy(FfiOpaque* value) {
    std::free(value);
}

}
