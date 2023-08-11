#include "okex_price_stream.hpp"

#include "crypto_utils.hpp"
#include "request_handler.hpp"
#include <spdlog/spdlog.h>

namespace jordan {

char const *const okex_price_stream_t::ws_host = "ws.okex.com";
char const *const okex_price_stream_t::ws_port_number = "8443";

okex_price_stream_t::okex_price_stream_t(net::io_context &io_context,
                                         net::ssl::context &ssl_ctx)
    : m_ioContext{io_context}, m_sslContext{ssl_ctx}, m_sslWebStream{},
      m_resolver{nullptr} {}

void okex_price_stream_t::run() { initiate_websocket_connection(); }

void okex_price_stream_t::initiate_websocket_connection() {
  m_sslWebStream.emplace(m_ioContext, m_sslContext);
  m_resolver = std::make_unique<net::ip::tcp::resolver>(m_ioContext);
  m_instrumentIDs.clear();
  m_pushedTypes = 0;

  m_resolver->async_resolve(
      ws_host, ws_port_number,
      [self = shared_from_this()](
          auto const error_code,
          net::ip::tcp::resolver::results_type const &results) {
        if (error_code) {
          return spdlog::error(error_code.message());
        }
        self->connect_to_resolved_names(results);
      });
}

void okex_price_stream_t::connect_to_resolved_names(
    results_type const &resolved_names) {

  m_resolver.reset();
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
            self->perform_ssl_handshake(connected_name);
          });
}

void okex_price_stream_t::perform_ssl_handshake(
    results_type::endpoint_type const &ep) {
  auto const host = ws_host + ':' + std::to_string(ep.port());

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

void okex_price_stream_t::perform_websocket_handshake() {
  static auto const okex_handshake_path = "/ws/v5/public";

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
      ws_host, okex_handshake_path,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec) {
          return spdlog::error(ec.message());
        }

        self->m_buffer.reset();
        self->subscribe_to_instruments_channels();
      });
}

void okex_price_stream_t::subscribe_to_instruments_channels() {
  json::array_t instruments;
  for (auto &&instr : {"SPOT", "SWAP", "FUTURES"}) {
    json::object_t instr_object;
    instr_object["channel"] = "instruments";
    instr_object["instType"] = instr;

    instruments.push_back(std::move(instr_object));
  }

  json::object_t subscribe_object;
  subscribe_object["op"] = "subscribe";
  subscribe_object["args"] = instruments;
  m_sendingBufferText.emplace(json(subscribe_object).dump(1));

  m_sslWebStream->async_write(
      net::buffer(*m_sendingBufferText),
      [self = shared_from_this()](beast::error_code const ec,
                                  std::size_t const) {
        if (ec) {
          spdlog::error(ec.message());
          return self->initiate_websocket_connection();
        }
        self->on_instruments_subscribed();
      });
}

void okex_price_stream_t::ticker_subscribe() {
  json::array_t instrument_list;
  for (auto const &instr : m_instrumentIDs) {
    json::object_t instr_object;
    instr_object["channel"] = "tickers";
    instr_object["instId"] = instr;
    instrument_list.push_back(instr_object);
  }

  json::object_t subscribe_object;
  subscribe_object["op"] = "subscribe";
  subscribe_object["args"] = instrument_list;
  m_instrumentIDs.clear();

  m_sendingBufferText.emplace(json(subscribe_object).dump(1));

  m_sslWebStream->async_write(
      net::buffer(*m_sendingBufferText),
      [self = shared_from_this()](beast::error_code const ec,
                                  std::size_t const) {
        if (ec) {
          spdlog::error(ec.message());
          return self->initiate_websocket_connection();
        }
        self->on_instruments_subscribed();
      });
}

void okex_price_stream_t::on_instruments_subscribed() {
  m_buffer.emplace();
  m_sslWebStream->async_read(
      *m_buffer, [self = shared_from_this()](beast::error_code const error_code,
                                             std::size_t const) {
        if (error_code) {
          spdlog::error(error_code.message());
          return self->initiate_websocket_connection();
        }
        self->interpret_generic_messages();
      });
}

void okex_price_stream_t::wait_for_messages() {
  m_buffer.emplace();
  m_sslWebStream->async_read(
      *m_buffer, [self = shared_from_this()](beast::error_code const error_code,
                                             std::size_t const) {
        if (error_code == net::error::operation_aborted) {
          return;
        } else if (error_code) {
          spdlog::error(error_code.message());
          self->m_sslWebStream.reset();
          return self->initiate_websocket_connection();
        }
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

  if (m_pushedTypes >= 3 && !m_instrumentIDs.empty()) {
    return ticker_subscribe();
  }

  return wait_for_messages();
}

trade_type_e tradeTypeFromString(std::string const &str) {
  if (str.empty() || str.size() < 2)
    throw std::runtime_error("invalid trade type: " + str);
  char const first = tolower(str[0]), second = tolower(str[1]);
  if (first == 'f' && second == 'u')
    return trade_type_e::futures;
  else {
    if (first == 's') {
      if (second == 'w')
        return trade_type_e::swap;
      else if (second == 'p')
        return trade_type_e::spot;
    }
    throw std::runtime_error("invalid trade type: " + str);
  }
}

void okex_price_stream_t::process_pushed_instruments_data(
    json::array_t const &data_list) {

  auto &allInstruments = request_handler_t::get_all_listed_instruments();
  auto &okexInstruments = allInstruments[(int)exchange_e::okex];

  for (auto const &data_json : data_list) {
    auto const data_object = data_json.get<json::object_t>();
    instrument_type_t order_info{};
    // FUTURES, SPOT, SWAP
    auto const type = (int)tradeTypeFromString(
        data_object.at("instType").get<json::string_t>());
    // BTC-USDT, DOGE-USDT
    okexInstruments[type].insert(
        data_object.at("instId").get<json::string_t>());
  }
  ++m_pushedTypes;
}

void okex_price_stream_t::process_pushed_tickers_data(
    json::array_t const &data_list) {
  auto &market_stream = request_handler_t::get_tokens_container();
  for (auto const &data_json : data_list) {
    auto const data_object = data_json.get<json::object_t>();

    pushed_subscription_data_t data{};
    data.symbolID = data_object.at("instId").get<json::string_t>();
    data.currentPrice = std::stod(data_object.at("last").get<json::string_t>());
    data.open24h = std::stod(data_object.at("sodUtc8").get<json::string_t>());
    market_stream.append(std::move(data));
  }
}

} // namespace jordan
