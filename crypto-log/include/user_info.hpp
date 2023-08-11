#pragma once

#include <string>
#include <vector>

#include "enumerations.hpp"

namespace jordan {

struct user_exchange_info_t {
  uint64_t userID = 0;
  std::string apiKey{};
  std::string secretKey{};
  std::string passphrase{};
  exchange_e exchange;
};

struct channel_info_t {
  social_channel_e channel;
  std::string id;
};

struct user_info_t {
  std::vector<user_exchange_info_t> exchanges;
  std::vector<channel_info_t> channels;
};

struct user_registration_data_t {
  std::string email;
  std::string username;
  std::string firstName;
  std::string lastName;
  std::string address;
  std::string passwordHash;
};
} // namespace jordan
