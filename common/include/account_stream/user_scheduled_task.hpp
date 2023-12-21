// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include "enumerations.hpp"
#include <string>

namespace keep_my_journal {
enum class task_operation_e {
  add,
  remove,
  update,
};

struct account_monitor_task_result_t {
  task_state_e state;
  std::string userID;
  std::string taskID;
#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(userID, state, taskID);
#endif
};

struct account_scheduled_task_t {
  std::string userID{};
  std::string taskID{};
  std::string apiKey{};
  std::string secretKey{};
  std::string passphrase{};
  exchange_e exchange;
  trade_type_e tradeType;
  task_operation_e operation;

#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(userID, taskID, apiKey, secretKey, passphrase, exchange,
                 tradeType, operation);
#endif
};

struct account_info_t {
  std::string userID;
  std::string apiKey{};
  std::string secretKey{};
  std::string passphrase{};

  friend bool operator==(account_info_t const &a, account_info_t const &b) {
    return (a.userID == b.userID) && (a.apiKey == b.apiKey) &&
           (a.secretKey == b.secretKey) && (a.passphrase == b.passphrase);
  }
};
} // namespace keep_my_journal

#ifdef CRYPTOLOG_USING_MSGPACK
MSGPACK_ADD_ENUM(keep_my_journal::task_operation_e)
#endif