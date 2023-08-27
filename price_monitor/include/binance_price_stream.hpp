#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <optional>
#include <set>

#include "commodity.hpp"
#include "json_utils.hpp"

namespace jordan {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websock = beast::websocket;
namespace http = beast::http;
namespace ip = net::ip;

class binance_price_stream_t
    : public std::enable_shared_from_this<binance_price_stream_t> {
  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

  char const *const m_restApiHost;
  char const *const m_wsHostname;
  char const *const m_wsPortNumber;
  net::io_context &m_ioContext;
  net::ssl::context &m_sslContext;
  std::set<instrument_type_t> &m_tradedInstruments;

  std::optional<resolver> m_resolver;
  std::optional<websock::stream<beast::ssl_stream<beast::tcp_stream>>>
      m_sslWebStream;
  std::optional<beast::flat_buffer> m_buffer;
  std::optional<http::request<http::empty_body>> m_httpRequest;
  std::optional<http::response<http::string_body>> m_httpResponse;

private:
  void rest_api_prepare_request();
  void rest_api_get_all_available_instruments();
  void rest_api_send_request();
  void rest_api_receive_response();
  void rest_api_on_data_received(beast::error_code const);
  void rest_api_initiate_connection();
  void rest_api_connect_to_resolved_names(results_type const &);
  void rest_api_perform_ssl_handshake(results_type::endpoint_type const &);

  void negotiate_websocket_connection();
  void initiate_websocket_connection();
  void websock_perform_ssl_handshake(results_type::endpoint_type const &);
  void websock_connect_to_resolved_names(results_type const &);
  void perform_websocket_handshake();
  void wait_for_messages();
  void process_pushed_tickers_data(json::array_t const &);
  void interpret_generic_messages();
  void process_pushed_instruments_data(json::array_t const &);

protected:
  virtual std::string rest_api_get_target() const = 0;

public:
  binance_price_stream_t(net::io_context &, net::ssl::context &,
                         trade_type_e const, char const *const restApiHost,
                         char const *const spotWsHost,
                         char const *const wsPortNumber);
  virtual ~binance_price_stream_t() = default;
  void run();
};

class binance_spot_price_stream_t : public binance_price_stream_t {
  static char const *const rest_api_host;
  static char const *const ws_host;
  static char const *const ws_port_number;

public:
  binance_spot_price_stream_t(net::io_context &, net::ssl::context &);
  std::string rest_api_get_target() const override {
    return "/api/v3/ticker/price";
  }
};

class binance_futures_price_stream_t : public binance_price_stream_t {
  static char const *const rest_api_host;
  static char const *const ws_host;
  static char const *const ws_port_number;

public:
  binance_futures_price_stream_t(net::io_context &, net::ssl::context &);
  std::string rest_api_get_target() const override {
    return "/fapi/v1/ticker/price";
  }
};

} // namespace jordan
