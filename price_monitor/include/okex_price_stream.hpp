#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <memory>
#include <optional>
#include <set>

#include "commodity.hpp"
#include "json_utils.hpp"

namespace jordan {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websock = beast::websocket;
namespace ip = net::ip;

class okex_price_stream_t
    : public std::enable_shared_from_this<okex_price_stream_t> {
  static char const *const ws_host;
  static char const *const ws_port_number;

  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

  net::io_context &m_ioContext;
  net::ssl::context &m_sslContext;
  std::set<instrument_type_t> &m_tradedInstruments;
  std::set<std::string> m_instruments{};
  std::unique_ptr<resolver> m_resolver;
  std::optional<websock::stream<beast::ssl_stream<beast::tcp_stream>>>
      m_sslWebStream;
  std::optional<std::string> m_sendingBufferText;
  std::optional<beast::flat_buffer> m_buffer;
  std::string const m_tradeType;

private:
  void initiate_websocket_connection();
  void connect_to_resolved_names(results_type const &);
  void perform_ssl_handshake(results_type::endpoint_type const &);
  void perform_websocket_handshake();
  void subscribe_to_instruments_channels();
  void on_instruments_subscribed();
  void wait_for_messages();
  void interpret_generic_messages();
  void process_pushed_instruments_data(json::array_t const &);
  void process_pushed_tickers_data(json::array_t const &);
  void ticker_subscribe();
  void report_error_and_retry(beast::error_code const ec);

public:
  okex_price_stream_t(net::io_context &, net::ssl::context &,
                      trade_type_e const tradeType);
  ~okex_price_stream_t() = default;
  void run();
};
} // namespace jordan
