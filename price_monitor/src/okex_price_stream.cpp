#include "okex_price_stream.hpp"

#include "crypto_utils.hpp"
#include <spdlog/spdlog.h>

#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

namespace jordan {

char const *const okex_price_stream_t::ws_host = "ws.okx.com";
char const *const okex_price_stream_t::ws_port_number = "8443";
char const *const okex_price_stream_t::api_host = "www.okx.com";
char const *const okex_price_stream_t::api_service = "https";

std::string trade_type_to_string(trade_type_e const t) {
  switch (t) {
  case trade_type_e::futures:
    return "FUTURES";
  case trade_type_e::spot:
    return "SPOT";
  case trade_type_e::swap:
    return "SWAP";
  default:
    return "UNKNOWN";
  }
}

okex_price_stream_t::okex_price_stream_t(net::io_context &ioContext,
                                         net::ssl::context &sslContext,
                                         trade_type_e const tradeType)
    : m_ioContext{ioContext}, m_sslContext{sslContext},
      m_tradedInstruments(
          instrument_sink_t::get_all_listed_instruments()[exchange_e::okex]
                                                         [tradeType]),
      m_sslWebStream{}, m_resolver{},
      m_tradeType(trade_type_to_string(tradeType)) {}

void okex_price_stream_t::run() { rest_api_initiate_connection(); }

void okex_price_stream_t::rest_api_initiate_connection() {
  m_resolver.emplace(m_ioContext);

  m_resolver->async_resolve(
      api_host, api_service,
      [self = shared_from_this()](
          auto const error_code,
          net::ip::tcp::resolver::results_type const &results) {
        if (error_code) {
          return spdlog::error(error_code.message());
        }
        self->rest_api_connect_to_resolved_names(results);
      });
}

void okex_price_stream_t::rest_api_connect_to_resolved_names(
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
}

void okex_price_stream_t::rest_api_perform_ssl_handshake(
    results_type::endpoint_type const &ep) {
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(10));
  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                api_host)) {
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

void okex_price_stream_t::rest_api_get_all_available_instruments() {
  rest_api_prepare_request();
  rest_api_send_request();
}

void okex_price_stream_t::rest_api_prepare_request() {
  using http::field;
  using http::verb;

  auto &request = m_httpRequest.emplace();
  request.method(verb::get);
  request.version(11);
  request.target("/api/v5/public/instruments?instType=" + m_tradeType);
  request.set(field::host, api_host);
  request.set(field::user_agent, "MyCryptoLog/0.0.1");
  request.set(field::accept, "*/*");
  request.set(field::accept_language, "en-US,en;q=0.5 --compressed");
}

void okex_price_stream_t::rest_api_send_request() {
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

void okex_price_stream_t::rest_api_receive_response() {
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

void okex_price_stream_t::rest_api_on_data_received(
    beast::error_code const ec) {
  if (ec)
    return spdlog::error(ec.message());

  try {
    auto const obj = json::parse(m_httpResponse->body()).get<json::object_t>();
    auto const codeIter = obj.find("code");
    if (codeIter == obj.end() || codeIter->second.get<json::string_t>() != "0")
      return;
    auto const dataIter = obj.find("data");
    if (dataIter == obj.end() || !dataIter->second.is_array())
      return;

    m_instruments.clear();
    process_pushed_instruments_data(dataIter->second.get<json::array_t>());
    initiate_websocket_connection();
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
}

void okex_price_stream_t::report_error_and_retry(beast::error_code const ec) {
  spdlog::error(ec.message());

  // wait a bit and then retry
  std::this_thread::sleep_for(std::chrono::seconds(2));
  rest_api_initiate_connection();
}

void okex_price_stream_t::initiate_websocket_connection() {
  m_resolver.emplace(m_ioContext);
  m_tradedInstruments.clear();

  m_resolver->async_resolve(
      okex_price_stream_t::ws_host, okex_price_stream_t::ws_port_number,
      [self = shared_from_this()](
          auto const error_code,
          net::ip::tcp::resolver::results_type const &results) {
        if (error_code)
          return self->report_error_and_retry(error_code);
        self->connect_to_resolved_names(results);
      });
}

void okex_price_stream_t::connect_to_resolved_names(
    results_type const &resolved_names) {

  m_resolver.reset();
  m_sslWebStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));

  beast::get_lowest_layer(*m_sslWebStream)
      .async_connect(resolved_names,
                     [self = shared_from_this()](
                         auto const error_code,
                         [[maybe_unused]] results_type::endpoint_type const
                             &connected_name) {
                       if (error_code)
                         return self->report_error_and_retry(error_code);
                       self->perform_ssl_handshake();
                     });
}

void okex_price_stream_t::perform_ssl_handshake() {
  // Set a timeout on the operation
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));

  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                ws_host)) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    return spdlog::error(ec.message());
  }

  m_sslWebStream->next_layer().async_handshake(
      net::ssl::stream_base::client,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec)
          return self->report_error_and_retry(ec);
        beast::get_lowest_layer(*self->m_sslWebStream).expires_never();
        return self->perform_websocket_handshake();
      });
}

void okex_price_stream_t::perform_websocket_handshake() {
  static auto const okex_handshake_path = "/ws/v5/public";

  auto opt = websock::stream_base::timeout();
  opt.idle_timeout = std::chrono::seconds(20);
  opt.handshake_timeout = std::chrono::seconds(5);
  opt.keep_alive_pings = true;

  m_sslWebStream->set_option(opt);
  m_sslWebStream->control_callback(
      [self = shared_from_this()](auto const frame_type, auto const &) {
        if (frame_type == websock::frame_type::close)
          return self->report_error_and_retry(beast::error_code{});
      });

  m_sslWebStream->async_handshake(
      ws_host, okex_handshake_path,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec)
          return self->report_error_and_retry(ec);

        self->m_buffer.reset();
        self->ticker_subscribe();
      });
}

void okex_price_stream_t::ticker_subscribe() {
  json::array_t instrument_list;
  for (auto const &instr : m_instruments) {
    json::object_t instr_object;
    instr_object["channel"] = "tickers";
    instr_object["instId"] = instr;
    instrument_list.push_back(instr_object);
  }

  json::object_t subscribe_object;
  subscribe_object["op"] = "subscribe";
  subscribe_object["args"] = instrument_list;
  m_instruments.clear();

  m_sendingBufferText.emplace(json(subscribe_object).dump(1));

  m_sslWebStream->async_write(
      net::buffer(*m_sendingBufferText),
      [self = shared_from_this()](beast::error_code const ec,
                                  std::size_t const) {
        if (ec)
          return self->report_error_and_retry(ec);
        self->on_tickers_subscribed();
      });
}

void okex_price_stream_t::on_tickers_subscribed() {
  m_buffer.emplace();
  m_sslWebStream->async_read(
      *m_buffer, [self = shared_from_this()](beast::error_code const error_code,
                                             std::size_t const) {
        if (error_code)
          return self->report_error_and_retry(error_code);
        self->interpret_generic_messages();
      });
}

void okex_price_stream_t::wait_for_messages() {
  m_buffer.emplace();
  m_sslWebStream->async_read(
      *m_buffer, [self = shared_from_this()](beast::error_code const error_code,
                                             std::size_t const) {
        if (error_code == net::error::operation_aborted)
          return;
        else if (error_code)
          return self->report_error_and_retry(error_code);
        self->interpret_generic_messages();
      });
}

void okex_price_stream_t::interpret_generic_messages() {
  char const *buffer_cstr = static_cast<char const *>(m_buffer->cdata().data());
  std::string_view const buffer(buffer_cstr, m_buffer->size());

  try {
    json::object_t const root = json::parse(buffer).get<json::object_t>();
    if (auto event_iter = root.find("event"); event_iter != root.end()) {
      if (auto const error_code = root.find("code");
          error_code != root.cend()) {
        // print error and continue as though nothing's happened
        spdlog::error(root.at("msg").get<json::string_t>());
      }
    } else if (auto data_iter = root.find("data"); data_iter != root.end()) {
      if (auto arg_iter = root.find("arg"); arg_iter != root.end()) {
        json::object_t const arg_object =
            arg_iter->second.get<json::object_t>();
        auto const channel = arg_object.at("channel").get<json::string_t>();
        if (channel == "instruments") {
          process_pushed_instruments_data(
              data_iter->second.get<json::array_t>());
        } else if (channel == "tickers") {
          process_pushed_tickers_data(data_iter->second.get<json::array_t>());
        }
      }
    } else {
      spdlog::info(buffer);
    }
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }

  return wait_for_messages();
}

void okex_price_stream_t::process_pushed_instruments_data(
    json::array_t const &data_list) {
  for (auto const &data_json : data_list) {
    auto const data_object = data_json.get<json::object_t>();
    instrument_type_t order_info{};
    // FUTURES, SPOT or SWAP
    auto const instrumentType =
        data_object.at("instType").get<json::string_t>();
    // BTC-USDT, DOGE-USDT
    if (instrumentType == m_tradeType)
      m_instruments.insert(data_object.at("instId").get<json::string_t>());
  }
}

void okex_price_stream_t::process_pushed_tickers_data(
    json::array_t const &data_list) {
  for (auto const &data_json : data_list) {
    auto const data_object = data_json.get<json::object_t>();

    instrument_type_t data{};
    data.name = data_object.at("instId").get<json::string_t>();
    data.current_price =
        std::stod(data_object.at("last").get<json::string_t>());
    data.open24h = std::stod(data_object.at("sodUtc8").get<json::string_t>());
    m_tradedInstruments.insert(std::move(data));
  }
}

void okexchange_price_watcher(net::io_context &ioContext,
                              net::ssl::context &sslContext) {
  auto spotStream = std::make_shared<okex_price_stream_t>(ioContext, sslContext,
                                                          trade_type_e::spot);

  auto swapStream = std::make_shared<okex_price_stream_t>(ioContext, sslContext,
                                                          trade_type_e::swap);

  auto futuresStream = std::make_shared<okex_price_stream_t>(
      ioContext, sslContext, trade_type_e::futures);

  spotStream->run();
  swapStream->run();
  futuresStream->run();

  ioContext.run();
}

} // namespace jordan
