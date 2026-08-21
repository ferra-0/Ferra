#include "file.h"
#include "global.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

std::string base_root;

namespace {

std::filesystem::path running_executable_path(
    const std::string& fallback
) {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(32768);
    const DWORD size = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0 && size < buffer.size()) {
        return std::filesystem::path(std::wstring(buffer.data(), size));
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size > 0) {
        std::vector<char> buffer(size);
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            return std::filesystem::path(buffer.data());
        }
    }
#elif defined(__linux__)
    std::vector<char> buffer(4096);
    const ssize_t size = readlink(
        "/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size > 0) {
        buffer[static_cast<size_t>(size)] = '\0';
        return std::filesystem::path(buffer.data());
    }
#endif
    return std::filesystem::path(fallback);
}

} // namespace

std::string fileread(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        gerror("Cannot open file: " + path + "\n");
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string get_ferra_path(const std::string& executable_path) {
    const char* environment_path = std::getenv("FERRA_PATH");
    if (environment_path != nullptr && environment_path[0] != '\0') {
        return std::string(environment_path);
    }

    if (executable_path.empty()) {
        return "";
    }

    std::error_code error;
    std::filesystem::path executable = std::filesystem::absolute(
        running_executable_path(executable_path), error);
    if (error) {
        executable = executable_path;
    }
    executable = std::filesystem::weakly_canonical(executable, error);
    if (error) {
        executable = std::filesystem::path(executable_path).lexically_normal();
    }

    const std::filesystem::path bin_dir = executable.parent_path();
    const std::filesystem::path candidates[] = {
        bin_dir / ".." / "share" / "ferra",
        bin_dir / "share" / "ferra",
        bin_dir / ".."
    };
    for (const auto& candidate : candidates) {
        const std::filesystem::path normalized = candidate.lexically_normal();
        if (std::filesystem::is_directory(normalized / "fe")) {
            return normalized.string();
        }
    }
    return "";
}
