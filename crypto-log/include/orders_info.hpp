#pragma once

#include <optional>
#include <string>

namespace jordan {

struct ws_order_info_t {
  std::string instrument_id{};
  std::string order_side{};
  std::string order_type{};
  std::string time_in_force{};
  std::string quantity_purchased{};
  std::string order_price{};
  std::string stop_price{};
  std::string execution_type{};
  std::string order_status{};
  std::string reject_reason{};
  std::string order_id{};
  std::string last_filled_quantity{};
  std::string cummulative_filled_quantity{};
  std::string last_executed_price{};
  std::string commission_amount{};
  std::string commission_asset{};
  std::string trade_id{};

  uint64_t event_time{};
  uint64_t transaction_time{};
  uint64_t created_time{};

  uint64_t userID{};
};

struct ws_balance_info_t {
  std::string instrument_id{};
  std::string balance{};
  uint64_t event_time{};
  uint64_t clear_time{};
  uint64_t userID = 0;
};

struct ws_account_update_t {
  std::string instrument_id{};
  std::string free_amount{};
  std::string locked_amount{};

  uint64_t event_time{};
  uint64_t last_account_update{};
  uint64_t userID = 0;
};

struct user_result_request_t {
  std::string account_alias{};
  std::optional<std::string> start_date{};
  std::optional<std::string> end_date{};
};

using instrument_type_t = std::string;
} // namespace jordan
