#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif


inline constexpr const char* compiler_platform_name() {
#if defined(__EMSCRIPTEN__)
    return "web";
#elif defined(__ANDROID__)
    return "android";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return "ios";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#elif defined(__FreeBSD__)
    return "freebsd";
#else
    return "unknown";
#endif
}
