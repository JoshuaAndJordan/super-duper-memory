#include "json_utils.hpp"

namespace keep_my_journal {
namespace utils {
std::string exchangesToString(exchange_e);
std::string tradeTypeToString(trade_type_e);
} // namespace utils

void to_json(json &j, scheduled_price_task_result_t const &data) {
  json::object_t obj;
  obj["results"] = data.result;
  obj["task"] = data.task;
  j = obj;
}

void to_json(json &j, scheduled_price_task_t const &data) {
  json::object_t obj;
  obj["task_id"] = data.task_id;
  obj["exchange"] = utils::exchangesToString(data.exchange);
  obj["trade_type"] = utils::tradeTypeToString(data.tradeType);
  obj["symbols"] = data.tokens;

  if (data.timeProp) {
    obj["intervals"] = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::milliseconds(data.timeProp->timeMS))
                           .count();
    obj["duration"] = "seconds";
  } else if (data.percentProp) {
    obj["direction"] = data.percentProp->percentage < 0 ? "down" : "up";
    obj["percentage"] = std::abs(data.percentProp->percentage);
  }

  j = obj;
}

void to_json(json &j, instrument_type_t const &instr) {
  j = json{{"name", instr.name},
           {"price", instr.currentPrice},
           {"open_24hr", instr.open24h},
           {"type", utils::tradeTypeToString(instr.tradeType)}};
}

void binance::to_json(json &j, ws_balance_info_t const &data) {
  j = json{{"user_id", data.userID},
           {"balance", data.balance},
           {"clear_time", data.clearTime},
           {"event_time", data.eventTime},
           {"symbol", data.instrumentID}};
}

void binance::to_json(json &j, ws_order_info_t const &data) {
  j = json{
      {"symbol", data.instrumentID},
      {"order_side", data.orderSide},
      {"order_type", data.orderType},
      {"time_in_force", data.timeInForce},
      {"quantity", data.quantityPurchased},
      {"order_price", data.orderPrice},
      {"stop_price", data.stopPrice},
      {"execution_type", data.executionType},
      {"status", data.orderStatus},
      {"rejection_reason", data.rejectReason},
      {"order_id", data.orderID},
      {"last_filled_qty", data.lastFilledQuantity},
      {"cumm_filled_qty", data.cumulativeFilledQuantity},
      {"last_executed_price", data.lastExecutedPrice},
      {"commission_amount", data.commissionAmount},
      {"commission_asset", data.commissionAsset},
      {"trade_id", data.tradeID},
      {"event_time", data.eventTime},
      {"transaction_time", data.transactionTime},
      {"created_time", data.createdTime},
      {"user_id", data.userID},
  };
}

void binance::to_json(json &j, ws_account_update_t const &data) {
  j = json{{"symbol", data.instrumentID},
           {"event_time", data.eventTime},
           {"user_id", data.userID},
           {"free_amount", data.freeAmount},
           {"last_update", data.lastAccountUpdate},
           {"locked_amount", data.lockedAmount}};
}

void okex::to_json(nlohmann::json &j, ws_order_info_t const &data) {
  j = json{{"instrument_type", data.instrumentType},
           {"symbol", data.instrumentID},
           {"currency", data.currency},
           {"order_id", data.orderID},
           {"order_price", data.orderPrice},
           {"qty_purchased", data.quantityPurchased},
           {"order_type", data.orderType},
           {"order_side", data.orderSide},
           {"position_side", data.positionSide},
           {"trade_mode", data.tradeMode},
           {"last_filled_qty", data.lastFilledQuantity},
           {"last_filled_free", data.lastFilledFee},
           {"last_filled_currency", data.lastFilledCurrency},
           {"state", data.state},
           {"fee_currency", data.feeCurrency},
           {"fee", data.fee},
           {"updated_time", data.updatedTime},
           {"created_time", data.createdTime},
           {"amend_result", data.amendResult},
           {"amend_err_message", data.amendErrorMessage},
           {"for_account", data.forAliasedAccount}};
}

void okex::to_json(nlohmann::json &j, ws_balance_data_t const &data) {
  j = json{{"balance", data.balance}, {"currency", data.currency}};
}
} // namespace keep_my_journal