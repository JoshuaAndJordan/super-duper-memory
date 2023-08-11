#pragma once

#include <string>
#include <vector>

namespace boost::asio {
namespace ssl {
class context;
}
class io_context;
} // namespace boost::asio

namespace net = boost::asio;
namespace ssl = net::ssl;

namespace jordan {
class binance_price_stream_t;

void background_price_retainer();
void task_scheduler_watcher(net::io_context &io_context);
void launch_prices_watcher(net::io_context &, ssl::context &ssl_context);
void persistent_account_records_saver(net::io_context &, ssl::context &ssl_context);
std::string get_alphanum_tablename(std::string);
} // namespace jordan
