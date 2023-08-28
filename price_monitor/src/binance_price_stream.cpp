#include "binance_price_stream.hpp"
#include "crypto_utils.hpp"

#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <spdlog/spdlog.h>

namespace jordan {

char const *const binance_spot_price_stream_t::rest_api_host =
    "api.binance.com";
char const *const binance_futures_price_stream_t::rest_api_host =
    "fapi.binance.com";
char const *const binance_spot_price_stream_t::ws_host = "stream.binance.com";
char const *const binance_futures_price_stream_t::ws_host =
    "fstream.binance.com";
char const *const binance_spot_price_stream_t::ws_port_number = "9443";
char const *const binance_futures_price_stream_t::ws_port_number = "443";

binance_price_stream_t::binance_price_stream_t(net::io_context &ioContext,
                                               net::ssl::context &sslContext,
                                               trade_type_e const tradeType,
                                               char const *const rest_api_host,
                                               char const *const ws_host,
                                               char const *const ws_port_number)
    : m_restApiHost(rest_api_host), m_wsHostname(ws_host),
      m_wsPortNumber(ws_port_number), m_ioContext{ioContext},
      m_sslContext{sslContext},
      m_tradedInstruments(
          instrument_sink_t::get_all_listed_instruments()[exchange_e::binance]
                                                         [tradeType]),
      m_resolver{}, m_sslWebStream{} {}

void binance_price_stream_t::run() { rest_api_initiate_connection(); }

void binance_price_stream_t::rest_api_initiate_connection() {
  m_resolver.emplace(m_ioContext);

  m_resolver->async_resolve(
      m_restApiHost, "https",
      [self = shared_from_this()](
          auto const error_code,
          net::ip::tcp::resolver::results_type const &results) {
        if (error_code) {
          return spdlog::error(error_code.message());
        }
        self->rest_api_connect_to_resolved_names(results);
      });
}

void binance_price_stream_t::rest_api_connect_to_resolved_names(
    results_type const &resolved_names) {

  m_resolver.reset();
  m_sslWebStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));

  beast::get_lowest_layer(*m_sslWebStream)
      .async_connect(resolved_names,
                     [self = shared_from_this()](auto const error_code,
                                                 auto const &connected_name) {
                       if (error_code)
                         return spdlog::error(error_code.message());
                       self->rest_api_perform_ssl_handshake(connected_name);
                     });
  spdlog::info("Connecting...");
}

void binance_price_stream_t::rest_api_perform_ssl_handshake(
    results_type::endpoint_type const &ep) {
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(15));
  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                m_restApiHost)) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    return spdlog::error(ec.message());
  }

  m_sslWebStream->next_layer().async_handshake(
      net::ssl::stream_base::client,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec)
          return spdlog::error(ec.message());
        return self->rest_api_get_all_available_instruments();
      });
}

void binance_price_stream_t::rest_api_get_all_available_instruments() {
  rest_api_prepare_request();
  rest_api_send_request();
}

void binance_price_stream_t::rest_api_prepare_request() {
  using http::field;
  using http::verb;

  auto &request = m_httpRequest.emplace();
  request.method(verb::get);
  request.version(11);
  request.target(rest_api_get_target());
  request.set(field::host, m_restApiHost);
  request.set(field::user_agent, "MyCryptoLog/0.0.1");
  request.set(field::accept, "*/*");
  request.set(field::accept_language, "en-US,en;q=0.5 --compressed");
}

void binance_price_stream_t::rest_api_send_request() {
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(20));
  http::async_write(m_sslWebStream->next_layer(), *m_httpRequest,
                    [self = shared_from_this()](beast::error_code const ec,
                                                std::size_t const) {
                      if (ec) {
                        return spdlog::error(ec.message());
                      }
                      self->rest_api_receive_response();
                    });
}

void binance_price_stream_t::rest_api_receive_response() {
  m_httpRequest.reset();
  m_buffer.emplace();
  m_httpResponse.emplace();

  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(20));
  http::async_read(
      m_sslWebStream->next_layer(), *m_buffer, *m_httpResponse,
      [self = shared_from_this()](beast::error_code ec, std::size_t const sz) {
        self->rest_api_on_data_received(ec);
      });
}

void binance_price_stream_t::rest_api_on_data_received(
    beast::error_code const ec) {
  if (ec) {
    return spdlog::error(ec.message());
  }

  try {
    auto const token_list =
        json::parse(m_httpResponse->body()).get<json::array_t>();
    process_pushed_instruments_data(token_list);
    return initiate_websocket_connection();
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
}

void binance_price_stream_t::initiate_websocket_connection() {
  m_resolver.emplace(m_ioContext);

  m_resolver->async_resolve(
      m_wsHostname, m_wsPortNumber,
      [self = shared_from_this()](
          auto const error_code,
          net::ip::tcp::resolver::results_type const &results) {
        if (error_code) {
          return spdlog::error(error_code.message());
        }
        self->websock_connect_to_resolved_names(results);
      });
}

void binance_price_stream_t::websock_connect_to_resolved_names(
    results_type const &resolved_names) {
  m_resolver.reset();
  m_sslWebStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));

  beast::get_lowest_layer(*m_sslWebStream)
      .async_connect(
          resolved_names,
          [self = shared_from_this()](
              auto const error_code,
              net::ip::tcp::resolver::results_type::endpoint_type const
                  &connected_name) {
            if (error_code) {
              return spdlog::error(error_code.message());
            }
            self->websock_perform_ssl_handshake(connected_name);
          });
}

void binance_price_stream_t::websock_perform_ssl_handshake(
    results_type::endpoint_type const &ep) {
  auto const host = fmt::format("{}:{}", m_wsHostname, ep.port());

  // Set a timeout on the operation
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));

  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                host.c_str())) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    return spdlog::error(ec.message());
  }
  negotiate_websocket_connection();
}

void binance_price_stream_t::negotiate_websocket_connection() {
  m_httpRequest.reset();
  m_httpResponse.reset();

  m_sslWebStream->next_layer().async_handshake(
      net::ssl::stream_base::client,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec) {
          return spdlog::error(ec.message());
        }
        beast::get_lowest_layer(*self->m_sslWebStream).expires_never();
        return self->perform_websocket_handshake();
      });
}

void binance_price_stream_t::perform_websocket_handshake() {
  static auto const binance_handshake_path = "/ws/!ticker@arr";

  auto opt = websock::stream_base::timeout();
  opt.idle_timeout = std::chrono::seconds(20);
  opt.handshake_timeout = std::chrono::seconds(5);
  opt.keep_alive_pings = true;
  m_sslWebStream->set_option(opt);

  m_sslWebStream->control_callback(
      [self = shared_from_this()](auto const frame_type, auto const &) {
        if (frame_type == websock::frame_type::close) {
          self->m_sslWebStream.reset();
          return self->initiate_websocket_connection();
        }
      });

  m_sslWebStream->async_handshake(
      m_wsHostname, binance_handshake_path,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec) {
          return spdlog::error(ec.message());
        }

        self->wait_for_messages();
      });
}

void binance_price_stream_t::wait_for_messages() {
  m_buffer.emplace();
  m_sslWebStream->async_read(
      *m_buffer, [self = shared_from_this()](beast::error_code const error_code,
                                             std::size_t const) {
        if (error_code == net::error::operation_aborted) {
          return spdlog::error(error_code.message());
        } else if (error_code) {
          spdlog::error(error_code.message());
          self->m_sslWebStream.reset();
          return self->initiate_websocket_connection();
        }
        self->interpret_generic_messages();
      });
}

void binance_price_stream_t::process_pushed_instruments_data(
    json::array_t const &data_list) {
  std::vector<instrument_type_t> instruments{};
  instruments.reserve(data_list.size());
  for (auto const &data_json : data_list) {
    auto const data_object = data_json.get<json::object_t>();
    instrument_type_t instrument{};
    instrument.name = data_object.at("symbol").get<json::string_t>();
    instruments.emplace_back(std::move(instrument));
  }

  m_tradedInstruments.insert(instruments.cbegin(), instruments.cend());
}

void binance_price_stream_t::interpret_generic_messages() {
  char const *buffer_cstr = static_cast<char const *>(m_buffer->cdata().data());
  std::string_view const buffer(buffer_cstr, m_buffer->size());

  try {
    auto object_list = json::parse(buffer).get<json::array_t>();
    process_pushed_tickers_data(std::move(object_list));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }

  return wait_for_messages();
}

void binance_price_stream_t::process_pushed_tickers_data(
    json::array_t const &data_list) {

  std::vector<instrument_type_t> pushed_list{};
  pushed_list.reserve(data_list.size());

  for (auto const &data_json : data_list) {
    instrument_type_t data{};
    auto const data_object = data_json.get<json::object_t>();
    // symbol => BTCDOGE, DOGEUSDT etc
    data.name = data_object.at("s").get<json::string_t>();
    data.current_price = std::stod(data_object.at("c").get<json::string_t>());
    data.open24h = std::stod(data_object.at("o").get<json::string_t>());
    pushed_list.push_back(std::move(data));
  }

  m_tradedInstruments.insert(pushed_list.begin(), pushed_list.end());
}

// ===========================================================

binance_spot_price_stream_t::binance_spot_price_stream_t(
    net::io_context &ioContext, net::ssl::context &sslContext)
    : binance_price_stream_t(ioContext, sslContext, trade_type_e::spot,
                             rest_api_host, ws_host, ws_port_number) {}

// ===========================================================
binance_futures_price_stream_t::binance_futures_price_stream_t(
    net::io_context &ioContext, net::ssl::context &sslContext)
    : binance_price_stream_t(ioContext, sslContext, trade_type_e::futures,
                             rest_api_host, ws_host, ws_port_number) {}

// ===========================================================

void binance_price_watcher(net::io_context &io_context,
                           net::ssl::context &ssl_context) {
  auto spot =
      std::make_shared<binance_spot_price_stream_t>(io_context, ssl_context);
  auto futures =
      std::make_shared<binance_futures_price_stream_t>(io_context, ssl_context);

  spot->run();
  futures->run();
  io_context.run();
}

} // namespace jordan
