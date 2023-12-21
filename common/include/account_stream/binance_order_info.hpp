// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include "container.hpp"
#include "enumerations.hpp"
#include <string>
#include <variant>

#ifdef CRYPTOLOG_USING_MSGPACK
#include <msgpack.hpp>
#endif

namespace keep_my_journal::binance {
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
  std::string cumulativeFilledQuantity{};
  std::string lastExecutedPrice{};
  std::string commissionAmount{};
  std::string commissionAsset{};
  std::string userID{};

  std::string tradeID{};
  uint64_t eventTime{};
  uint64_t transactionTime{};
  uint64_t createdTime{};

#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(instrumentID, orderSide, orderType, timeInForce,
                 quantityPurchased, orderPrice, stopPrice, executionType,
                 orderStatus, rejectReason, orderID, lastFilledQuantity,
                 cumulativeFilledQuantity, lastExecutedPrice, commissionAmount,
                 commissionAsset, tradeID, eventTime, transactionTime,
                 createdTime, userID);
#endif
};

struct ws_balance_info_t {
  std::string instrumentID{};
  std::string balance{};
  std::string userID{};
  uint64_t eventTime = 0;
  uint64_t clearTime = 0;

#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(instrumentID, balance, eventTime, clearTime, userID);
#endif
};

struct ws_account_update_t {
  std::string instrumentID;
  std::string freeAmount;
  std::string lockedAmount;
  std::string userID;

  uint64_t eventTime = 0;
  uint64_t lastAccountUpdate = 0;

#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(instrumentID, freeAmount, lockedAmount, eventTime,
                 lastAccountUpdate, userID);
#endif
};

using stream_data_t =
    std::variant<ws_balance_info_t, ws_order_info_t, ws_account_update_t>;
using binance_result_t = utils::waitable_container_t<stream_data_t>;

struct account_stream_sink_t {
  static auto &get_account_stream() {
    static binance_result_t accountStreamsSink{};
    return accountStreamsSink;
  }
};
} // namespace keep_my_journal::binance