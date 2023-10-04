// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace keep_my_journal {
class db_config_t;
}

namespace keep_my_journal::utils {
void trimString(std::string &);

template <typename T> using filter_t = bool (*)(std::string_view const, T &);

template <typename T, typename Func>
void getFileContent(std::string const &filename, filter_t<T> filter,
                    Func post_op) {
  std::ifstream in_file{filename};
  if (!in_file)
    return;
  std::string line{};
  T output{};
  while (std::getline(in_file, line)) {
    trimString(line);
    if (line.empty())
      continue;
    if (filter(line, output))
      post_op(output);
  }
}

bool createFileDirectory(std::filesystem::path const &path);
void normalizePaths(std::string &str);
void replaceSpecialChars(std::string &str);
void removeFile(std::string &filename);
std::unique_ptr<keep_my_journal::db_config_t>
parseConfigFile(std::string const &filename, std::string const &config_name);
} // namespace keep_my_journal::utils
