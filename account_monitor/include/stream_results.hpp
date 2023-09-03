#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>

#include "container.hpp"
#include "enumerations.hpp"

namespace jordan::binance {

struct ws_order_info_t {
  std::string instrumentID{};
  std::string orderSide{};
  std::string orderType{};
  std::string timeInForce{};
  std::string quantityPurchased{};
  std::string orderPrice{};
  std::string stopPrice{};
  std::string executionType{};
  std::string orderStatus{};
  std::string rejectReason{};
  std::string orderID{};
  std::string lastFilledQuantity{};
  std::string cummulativeFilledQuantity{};
  std::string lastExecutedPrice{};
  std::string commissionAmount{};
  std::string commissionAsset{};
  std::string tradeID{};

  uint64_t eventTime{};
  uint64_t transactionTime{};
  uint64_t createdTime{};
  uint64_t userID{};
};

struct ws_balance_info_t {
  std::string instrumentID{};
  std::string balance{};
  uint64_t eventTime{};
  uint64_t clearTime{};
  uint64_t userID = 0;
};

struct ws_account_update_t {
  std::string instrumentID{};
  std::string freeAmount{};
  std::string lockedAmount{};

  uint64_t eventTime{};
  uint64_t lastAccountUpdate{};
  uint64_t userID = 0;
};

struct user_result_request_t {
  std::string accountAlias{};
  std::optional<std::string> startDate{};
  std::optional<std::string> endDate{};
};

using stream_result_t =
    std::variant<ws_balance_info_t, ws_order_info_t, ws_account_update_t>;

struct account_stream_sink_t {
  static auto &get_account_stream(exchange_e const exchange) {
    static std::map<exchange_e, utils::waitable_container_t<stream_result_t>>
        accountStreamsSink{};
    return accountStreamsSink[exchange];
  }
};

using instrument_type_t = std::string;
} // namespace jordan::binance
