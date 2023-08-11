#include "websock_launcher.hpp"
#include "binance_price_stream.hpp"
#include "okex_price_stream.hpp"
#include "request_handler.hpp"
#include "tasks.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <spdlog/spdlog.h>

namespace jordan {

void launch_prices_watcher(net::io_context &io_context,
                           ssl::context &ssl_context) {
  std::make_shared<binance_spot_price_stream_t>(io_context, ssl_context)->run();
  std::make_shared<binance_futures_price_stream_t>(io_context, ssl_context)
      ->run();
  std::make_shared<okex_price_stream_t>(io_context, ssl_context)->run();
}

void background_price_retainer() {
  auto &token_container = request_handler_t::get_tokens_container();
  auto &pushed_subs = request_handler_t::get_all_pushed_data();

  while (true) {
    auto item = token_container.get();
    auto &data = pushed_subs[item.symbolID];
    data.currentPrice = item.currentPrice;
    data.open24h = item.open24h;
  }
}

void persistent_account_records_saver(net::io_context &io_context,
                                      ssl::context &ssl_context) {
  auto &stream_container = request_handler_t::getUserExchangeResults();

  while (true) {
    auto item_var = stream_container.get();

    std::visit(
        [&](auto &&item) {
          using item_type = std::decay_t<decltype(item)>;
          if constexpr (std::is_same_v<item_type, ws_order_info_t>) {
          } else if constexpr (std::is_same_v<item_type, ws_balance_info_t>) {
          } else {
          }
        },
        item_var);
  }
}

} // namespace jordan
