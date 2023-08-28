#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
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
namespace http = boost::beast::http;

class okex_price_stream_t
    : public std::enable_shared_from_this<okex_price_stream_t> {
  static char const *const ws_host;
  static char const *const ws_port_number;
  static char const *const api_host;
  static char const *const api_service;

  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

  net::io_context &m_ioContext;
  net::ssl::context &m_sslContext;
  std::set<instrument_type_t> &m_tradedInstruments;
  std::set<std::string> m_instruments{};
  std::optional<resolver> m_resolver;
  std::optional<websock::stream<beast::ssl_stream<beast::tcp_stream>>>
      m_sslWebStream;
  std::optional<http::request<http::empty_body>> m_httpRequest;
  std::optional<http::response<http::string_body>> m_httpResponse;

  std::optional<std::string> m_sendingBufferText;
  std::optional<beast::flat_buffer> m_buffer;
  std::string const m_tradeType;

private:
  void rest_api_initiate_connection();
  void rest_api_prepare_request();
  void rest_api_get_all_available_instruments();
  void rest_api_send_request();
  void rest_api_receive_response();
  void rest_api_on_data_received(beast::error_code const);
  void rest_api_connect_to_resolved_names(results_type const &);
  void rest_api_perform_ssl_handshake(results_type::endpoint_type const &);

  void initiate_websocket_connection();
  void connect_to_resolved_names(results_type const &);
  void perform_ssl_handshake();
  void perform_websocket_handshake();
  void on_tickers_subscribed();
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
