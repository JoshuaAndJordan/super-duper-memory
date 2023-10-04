// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <string>

namespace keep_my_journal {

struct db_config_t {
  std::string dbUsername{};
  std::string dbPassword{};
  std::string dbDns{};
  std::string jwtSecretKey{};
  int softwareClientVersion = 0;
  int softwareServerVersion = 0;

  operator bool() {
    return !(dbUsername.empty() && dbPassword.empty() && dbDns.empty() &&
             softwareClientVersion == 0);
  }
};
} // namespace keep_my_journal
