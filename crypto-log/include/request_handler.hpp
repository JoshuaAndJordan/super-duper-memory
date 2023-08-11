#pragma once

#include "container.hpp"
#include "orders_info.hpp"
#include "subscription_data.hpp"
#include "user_info.hpp"
#include <array>
#include <variant>

namespace jordan {

using user_stream_result_t =
    std::variant<ws_order_info_t, ws_balance_info_t, ws_account_update_t>;

class request_handler_t {
  // like a map<exchange_name, map<futures/spot/swap, symbol>
  using trade_symbols_map_t =
      std::array<utils::locked_set_t<instrument_type_t>,
                 static_cast<size_t>(trade_type_e::total)>;
  using exchange_symbols_map_t =
      std::array<trade_symbols_map_t, static_cast<size_t>(exchange_e::total)>;

  static utils::waitable_container_t<user_stream_result_t> userStreamContainer;
  static utils::waitable_container_t<pushed_subscription_data_t>
      tokensContainer;
  static exchange_symbols_map_t allListedInstruments;
  static subscription_data_map_t allPushedSubData;

public:
  static auto &get_tokens_container() { return tokensContainer; }
  static auto &get_all_listed_instruments() { return allListedInstruments; }
  static auto &get_all_pushed_data() { return allPushedSubData; }
  static auto &getUserExchangeResults() { return userStreamContainer; }
};

} // namespace jordan
