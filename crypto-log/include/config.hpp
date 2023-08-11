#pragma once

#include <string>

namespace jordan {

struct db_config_t {
  std::string db_username{};
  std::string db_password{};
  std::string db_dns{};
  std::string jwt_secret_key{};
  std::string tg_bot_secret_key{};
  int software_client_version{};
  int software_server_version{};

  operator bool() {
    return !(db_username.empty() && db_password.empty() && db_dns.empty() &&
             software_client_version == 0);
  }
};

struct api_key_data_t {
  std::string key{};
  std::string alias_for_account{};
};

struct login_token_info_t {
  int user_id{};
  int user_role{-1};
  std::string bearer_token{};
};

} // namespace jordan
