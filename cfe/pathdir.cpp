#include <filesystem>
#include <string>

namespace fs = std::filesystem;

struct DirIter {
  fs::directory_iterator it;
  fs::directory_iterator end;

  fs::directory_entry current;
};

extern "C" {

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

  out = fs::absolute(path).string();

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