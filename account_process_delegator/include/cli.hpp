// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#pragma once

#include <string>

namespace keep_my_journal {
struct command_line_interface_t {
  uint16_t port{3421};
  std::string ip_address{"127.0.0.1"};
  std::string launch_type{"development"};
  std::string database_config_filename{"../config/info.json"};
};
} // namespace keep_my_journal
