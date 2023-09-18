// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <string>
#include "enumerations.hpp"

namespace jordan {
  enum class task_operation_e {
    add,
    remove,
    update,
  };

  struct account_scheduled_task_t {
    int64_t userID{};
    std::string apiKey{};
    std::string secretKey{};
    std::string passphrase{};
    exchange_e  exchange;
    trade_type_e tradeType;
    task_operation_e operation;

#ifdef CRYPTOLOG_USING_MSGPACK
    MSGPACK_DEFINE(userID, apiKey, secretKey, passphrase, exchange, tradeType, operation);
#endif
  };

  struct account_info_t {
    int64_t     userID = 0;
    std::string apiKey{};
    std::string secretKey{};
    std::string passphrase{};

    friend bool operator==(account_info_t const &a, account_info_t const &b) {
      return (a.userID == b.userID) && (a.apiKey == b.apiKey) &&
          (a.secretKey == b.secretKey) && (a.passphrase == b.passphrase);
    }
  };
}

#ifdef CRYPTOLOG_USING_MSGPACK
MSGPACK_ADD_ENUM(jordan::task_operation_e);
#endif