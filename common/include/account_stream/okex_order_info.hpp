// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#pragma once

#include "container.hpp"
#include <string>
#include <variant>

#ifdef CRYPTOLOG_USING_MSGPACK
#include <msgpack.hpp>
#endif

namespace keep_my_journal::okex {
struct ws_order_info_t {
  std::string instrumentType{};
  std::string instrumentID{};
  std::string currency{};
  std::string orderID{};
  std::string orderPrice{};
  std::string quantityPurchased{};
  std::string orderType{};
  std::string orderSide{};
  std::string positionSide{};
  std::string tradeMode{};
  std::string lastFilledQuantity{};
  std::string lastFilledFee{};
  std::string lastFilledCurrency{};
  std::string state{};
  std::string feeCurrency{};
  std::string fee{};
  std::string updatedTime{};
  std::string createdTime{};
  std::string amendResult{};
  std::string amendErrorMessage{};
  std::string forAliasedAccount{};

#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(instrumentType, instrumentID, currency, orderID, orderPrice,
                 quantityPurchased, orderType, orderSide, positionSide,
                 tradeMode, lastFilledQuantity, lastFilledFee,
                 lastFilledCurrency, state, feeCurrency, fee, updatedTime,
                 createdTime, amendResult, amendErrorMessage,
                 forAliasedAccount);
#endif
};

struct ws_balance_data_t {
  std::string balance;
  std::string currency;
#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(balance, currency);
#endif
};

using okex_ws_data_t = std::variant<ws_balance_data_t, ws_order_info_t>;
using okex_container_t = utils::waitable_container_t<okex_ws_data_t>;

struct account_stream_sink_t {
  static auto &get_account_stream() {
    static okex_container_t accountStreamsSink{};
    return accountStreamsSink;
  }
};

} // namespace keep_my_journal::okex
