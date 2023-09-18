#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <optional>
#include <set>

#include "commodity.hpp"
#include "json_utils.hpp"

namespace jordan {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace http = beast::http;
namespace ip = net::ip;

class https_rest_api_t;

class binance_price_stream_t
    : public std::enable_shared_from_this<binance_price_stream_t> {
  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

  char const *const m_restApiHost;
  char const *const m_wsHostname;
  char const *const m_wsPortNumber;
  net::io_context &m_ioContext;
  net::ssl::context &m_sslContext;
  instrument_sink_t::list_t &m_tradedInstruments;
  trade_type_e const m_tradeType;

  std::optional<resolver> m_resolver;
  std::optional<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>
      m_sslWebStream;
  std::optional<beast::flat_buffer> m_buffer;
  std::unique_ptr<https_rest_api_t> m_httpClient = nullptr;

private:
  void rest_api_initiate_connection();
  void rest_api_on_data_received(std::string const &);
  void negotiate_websocket_connection();
  void initiate_websocket_connection();
  void websocket_perform_ssl_handshake(results_type::endpoint_type const &);
  void websocket_connect_to_resolved_names(results_type const &);
  void perform_websocket_handshake();
  void wait_for_messages();
  void process_pushed_tickers_data(json::array_t const &);
  void interpret_generic_messages();
  void process_pushed_instruments_data(json::array_t const &);

protected:
  virtual std::string rest_api_get_target() const = 0;

public:
  binance_price_stream_t(net::io_context &, net::ssl::context &,
                         trade_type_e , char const *restApiHost,
                         char const *spotWsHost,
                         char const *wsPortNumber);
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
