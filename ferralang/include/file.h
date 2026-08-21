#pragma once

#include <string>

std::string fileread(const std::string& path);

extern std::string base_root;

std::string get_ferra_path(const std::string& executable_path = "");
