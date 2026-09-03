#include <filesystem>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

struct DirIter {
  fs::directory_iterator it;
  fs::directory_iterator end;

  fs::directory_entry current;
};

extern "C" {

const char* path_ferra_root() {
  static std::string out;
  const char* environment = std::getenv("FERRA_PATH");
  if (environment != nullptr && environment[0] != '\0') {
    out = fs::absolute(environment).lexically_normal().string();
    return out.c_str();
  }

  fs::path executable;
#if defined(_WIN32)
  std::wstring buffer(32768, L'\0');
  const DWORD size = GetModuleFileNameW(
    nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size > 0 && size < buffer.size()) {
    buffer.resize(size);
    executable = fs::path(buffer);
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  if (size > 0) {
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
      executable = fs::path(buffer.c_str());
    }
  }
#elif defined(__linux__)
  std::string buffer(4096, '\0');
  const ssize_t size = readlink(
    "/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (size > 0) {
    buffer.resize(static_cast<size_t>(size));
    executable = fs::path(buffer);
  }
#endif

  if (executable.empty()) {
    out.clear();
    return out.c_str();
  }
  const fs::path bin_dir = executable.parent_path();
  const fs::path candidates[] = {
    bin_dir / ".." / "share" / "ferra",
    bin_dir / "share" / "ferra",
    bin_dir / ".."
  };
  for (const fs::path& candidate : candidates) {
    const fs::path normalized = candidate.lexically_normal();
    if (fs::is_directory(normalized / "fe")) {
      out = normalized.string();
      return out.c_str();
    }
  }
  out.clear();
  return out.c_str();
}

const char* path_join(const char* a, const char* b) {
  static std::string out;

  if (!a || !b) {
    out.clear();
    return out.c_str();
  }

  out = (fs::path(a) / fs::path(b)).string();

  return out.c_str();
}

const char* path_filename(const char* path) {
  static std::string out;

  if (!path) {
    out.clear();
    return out.c_str();
  }

  out = fs::path(path)
    .filename()
    .string();

  return out.c_str();
}

const char* path_extension(const char* path) {
  static std::string out;

  if (!path) {
    out.clear();
    return out.c_str();
  }

  out = fs::path(path)
    .extension()
    .string();

  return out.c_str();
}

const char* path_parent(const char* path) {
  static std::string out;

  if (!path) {
    out.clear();
    return out.c_str();
  }

  out = fs::path(path)
    .parent_path()
    .string();

  return out.c_str();
}

const char* path_stem(const char* path) {
  static std::string out;

  if (!path) {
    out.clear();
    return out.c_str();
  }

  out = fs::path(path)
    .stem()
    .string();

  return out.c_str();
}

const char* path_absolute(const char* path) {
  static std::string out;

  if (!path) {
    out.clear();
    return out.c_str();
  }

  out = fs::absolute(path).lexically_normal().string();

  return out.c_str();
}

bool path_exists(const char* path) {
  if (!path) return false;

  return fs::exists(path);
}

bool directory_exists(const char* path) {
  if (!path) return false;

  return fs::is_directory(path);
}

bool directory_create(const char* path) {
  if (!path) return false;

  std::error_code ec;

  return fs::create_directories(
    path,
    ec
  );
}

bool directory_remove(const char* path) {
  if (!path) return false;

  std::error_code ec;

  return fs::remove_all(
    path,
    ec
  ) > 0;
}

DirIter* dir_open(const char* path) {
  if (!path) return nullptr;

  try {
    DirIter* d = new DirIter;

    d->it = fs::directory_iterator(path);
    d->end = fs::directory_iterator();

    return d;
  }
  catch (...) {
    return nullptr;
  }
}

void dir_close(DirIter* d) {
  delete d;
}

bool dir_next(DirIter* d) {
  if (!d) return false;

  if (d->it == d->end) return false;

  d->current = *d->it;

  ++d->it;

  return true;
}

const char* dir_name(DirIter* d) {
  static std::string out;

  if (!d) return "";

  out = d->current.path().filename().string();

  return out.c_str();
}

const char* dir_path(DirIter* d) {
  static std::string out;

  if (!d) return "";

  out = d->current.path().string();

  return out.c_str();
}

bool dir_is_dir(DirIter* d) {
  if (!d) return false;

  return d->current.is_directory();
}

bool dir_is_file(DirIter* d) {
  if (!d) return false;

  return d->current.is_regular_file();
}

long long dir_size(DirIter* d) {
  if (!d) return 0;

  if (!d->current.is_regular_file()) return 0;

  std::error_code ec;

  return (long long)d->current.file_size(ec);
}

}
