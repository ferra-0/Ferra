#pragma once

#include <iostream>
#include <string>

inline bool g_had_compilation_error = false;

inline void gerror(const std::string& message) {
    g_had_compilation_error = true;
    std::cerr << "\033[31m" << message << "\033[0m";
}
