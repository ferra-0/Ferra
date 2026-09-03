#pragma once

#include <iostream>
#include <string>
#include "tokens.h"

inline bool g_had_compilation_error = false;
inline int gdiagnostic_line = 0;

class ScopedDiagnosticLine {
public:
    explicit ScopedDiagnosticLine(int line)
        : previous_(gdiagnostic_line) {
        if (line > 0) gdiagnostic_line = line;
    }

    ~ScopedDiagnosticLine() {
        gdiagnostic_line = previous_;
    }

private:
    int previous_;
};

inline int current_diagnostic_line() {
    return gdiagnostic_line > 0 ? gdiagnostic_line : gline;
}

inline void gerror(const std::string& message) {
    g_had_compilation_error = true;
    std::cerr << current_diagnostic_line() << ": "
              << "\033[31m" << message << "\033[0m";
}

inline void gwarn(const std::string& message) {
    std::cerr << current_diagnostic_line() << ": "
              << "\033[33mwarning: " << message << "\033[0m";
}

inline void gerror_at(int line, const std::string& message) {
    ScopedDiagnosticLine scope(line);
    gerror(message);
}

inline void gwarn_at(int line, const std::string& message) {
    ScopedDiagnosticLine scope(line);
    gwarn(message);
}
