// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include "okex_user_stream.hpp"
#include "crypto_utils.hpp"
#include <spdlog/spdlog.h>

namespace keep_my_journal {
namespace utils {
std::optional<std::string> okexMsTimeToString(std::size_t const t) {
#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif

  try {
    auto current_time = (std::time_t)t;
    auto const tm_t = std::gmtime(&current_time);
    if (!tm_t)
      return std::nullopt;

    std::string output((std::size_t)32, (char)'\0');
    auto const format = "%Y-%m-%d %H:%M:%S";
    auto const string_length =
        std::strftime(output.data(), output.size(), format, tm_t);
    if (string_length) {
      output.resize(string_length);
      return output;
    }
  } catch (std::exception const &) {
  }
  return std::nullopt;
}

std::optional<std::string> okexMsTimeToString(std::string const &str) {
  if (str.empty())
    return std::nullopt;

  try {
    auto const t = static_cast<std::size_t>(std::stoull(str)) / 1'000;
    return okexMsTimeToString(t);
  } catch (std::exception const &) {
  }
  return std::nullopt;
}
} // namespace utils

char const *const okex_stream_t::ws_api_host = "ws.okx.com";
char const *const okex_stream_t::ws_api_service = "https";

okex_stream_t::okex_stream_t(net::io_context &ioContext,
                             ssl::context &sslContext,
                             account_info_t &&accountInfo)
    : m_ioContext{ioContext}, m_sslContext{sslContext},
      m_sslWebStream(std::nullopt), m_resolver(std::nullopt),
      m_accountInfo(std::move(accountInfo)),
      m_streamResult(okex::account_stream_sink_t::get_account_stream()) {}

void okex_stream_t::run() { initiate_websocket_connection(); }

void okex_stream_t::stop() {
  m_stopped = true;

  if (m_sslWebStream) {
    m_sslWebStream->async_close(
        websocket::close_reason{},
        [self = shared_from_this()](beast::error_code const ec) {
          spdlog::error(ec.message());
        });
  }
}

void okex_stream_t::on_ws_connection_severed(std::string const &errorString) {
  spdlog::error(errorString);

  if (m_sslWebStream) {
    m_sslWebStream->close({});
    m_sslWebStream.reset();
  }

  m_buffer.reset();
  m_stopped = false;

  m_timer.emplace(m_ioContext);
  m_timer->expires_after(std::chrono::seconds(10));
  m_timer->async_wait([self = shared_from_this()](auto const &ec) {
    if (ec)
      return spdlog::error(ec.message());

    self->initiate_websocket_connection();
  });
}

void okex_stream_t::initiate_websocket_connection() {
  if (m_stopped)
    return;

  m_resolver.emplace(m_ioContext);
  m_resolver->async_resolve(
      ws_api_host, ws_api_service,
      [self = shared_from_this()](
          auto const error_code,
          net::ip::tcp::resolver::results_type const &results) {
        if (error_code)
          return self->on_ws_connection_severed(error_code.message());
        self->connect_to_resolved_names(results);
      });
}

void okex_stream_t::connect_to_resolved_names(
    net::ip::tcp::resolver::results_type const &resolved_names) {

  m_resolver.reset();
  m_timer.reset();

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
            if (error_code)
              return self->on_ws_connection_severed(error_code.message());
            self->perform_ssl_handshake(connected_name);
          });
}

void okex_stream_t::perform_ssl_handshake(
    resolver_results_t::endpoint_type const &ep) {
  auto const host = fmt::format("{}:{}", ws_api_host, ep.port());

  // Set a timeout on the operation
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));

  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                host.c_str())) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    return on_ws_connection_severed(ec.message());
  }

  m_sslWebStream->next_layer().async_handshake(
      ssl::stream_base::client,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec)
          return self->on_ws_connection_severed(ec.message());
        beast::get_lowest_layer(*self->m_sslWebStream).expires_never();
        return self->perform_websocket_handshake();
      });
}

void okex_stream_t::perform_websocket_handshake() {
  static auto const okex_handshake_path = "/ws/v5/private";
  auto opt = websocket::stream_base::timeout();

  opt.idle_timeout = std::chrono::seconds(25);
  opt.handshake_timeout = std::chrono::seconds(20);
  // enable the automatic keepalive pings
  opt.keep_alive_pings = true;
  m_sslWebStream->set_option(opt);
  m_sslWebStream->control_callback(
      [self = shared_from_this()](auto const frame_type, auto const &) {
        if (frame_type == websocket::frame_type::close) {
          if (!self->m_stopped)
            return self->on_ws_connection_severed("remote reset connection");
        } else if (frame_type == websocket::frame_type::pong) {
          spdlog::info("pong...");
        }
      });

  m_sslWebStream->async_handshake(
      ws_api_host, ws_api_service,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec)
          return self->on_ws_connection_severed(ec.message());
        // if we're here, everything went well.
        self->perform_user_login();
      });
}

void okex_stream_t::perform_user_login() {

  // concatenate timestamp, method, requestPath, and body strings, then use
  // HMAC SHA256 method to encrypt the concatenated string with SecretKey,
  // and then perform Base64 encoding.
  std::string const unix_epoch_time = std::to_string(std::time(nullptr));
  auto const method = "GET";
  auto const request_path = "/users/self/verify";
  auto const concatenated_data = unix_epoch_time + method + request_path;
  auto const sign = utils::base64Encode(
      utils::hmac256Encode(concatenated_data, m_accountInfo->secretKey));

  json::object_t args_object;
  args_object["apiKey"] = m_accountInfo->apiKey;
  args_object["passphrase"] = m_accountInfo->passphrase;
  args_object["timestamp"] = unix_epoch_time;
  args_object["sign"] = sign;
  json::array_t args;
  args.push_back(args_object);

  json::object_t login_info;
  login_info["op"] = "login";
  login_info["args"] = args;

  m_sendingBufferText.emplace(json(login_info).dump(1));
  m_sslWebStream->async_write(
      net::buffer(*m_sendingBufferText),
      [self = shared_from_this()](beast::error_code const ec,
                                  std::size_t const) {
        if (ec)
          return self->on_ws_connection_severed(ec.message());
        self->read_login_response();
      });
}

void okex_stream_t::read_login_response() {
  m_sendingBufferText.reset();
  m_buffer.emplace();
  m_sslWebStream->async_read(
      *m_buffer,
      [self = shared_from_this()](auto const error_code, std::size_t const) {
        if (error_code)
          return self->on_ws_connection_severed(error_code.message());
        self->interpret_login_response();
      });
}

void okex_stream_t::interpret_login_response() {
  char const *buffer_cstr = static_cast<char const *>(m_buffer->cdata().data());
  std::string_view const buffer(buffer_cstr, m_buffer->size());

  try {
    json::object_t const object = json::parse(buffer).get<json::object_t>();
    auto const code = object.at("code").get<json::string_t>();
    if (code != "0")
      return on_ws_connection_severed(object.at("msg").get<json::string_t>());
  } catch (std::exception const &e) {
    return on_ws_connection_severed(e.what());
  }
  m_buffer.reset();
  subscribe_to_orders_channels();
}

void okex_stream_t::subscribe_to_orders_channels() {
  json::array_t instruments;
  for (auto &&instr : {"SPOT", "SWAP", "FUTURES"}) {
    json::object_t instr_object;
    instr_object["channel"] = "orders";
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
        if (ec)
          return self->on_ws_connection_severed(ec.message());
        self->on_instruments_subscribed();
      });
}

void okex_stream_t::subscribe_to_accounts_channel() {
  m_sendingBufferText.emplace("{\"op\": \"subscribe\",\"args\" : [{"
                              "\"channel\": \"balance_and_position\"}]}");
  m_sslWebStream->async_write(
      net::buffer(*m_sendingBufferText),
      [self = shared_from_this()](beast::error_code const ec,
                                  std::size_t const) {
        if (ec)
          return self->on_ws_connection_severed(ec.message());
        self->on_instruments_subscribed();
      });
}

void okex_stream_t::on_instruments_subscribed() {
  m_buffer.emplace();
  m_sslWebStream->async_read(
      *m_buffer, [self = shared_from_this()](beast::error_code const error_code,
                                             std::size_t const) {
        if (error_code)
          return self->on_ws_connection_severed(error_code.message());
        self->interpret_generic_messages();
      });
}

void okex_stream_t::wait_for_messages() {
  m_buffer.emplace();
  m_sslWebStream->async_read(
      *m_buffer, [self = shared_from_this()](beast::error_code const error_code,
                                             std::size_t const) {
        if (error_code)
          return self->on_ws_connection_severed(error_code.message());
        self->interpret_generic_messages();
      });
}

void okex_stream_t::interpret_generic_messages() {
  char const *const buffer_cstr =
      static_cast<char const *>(m_buffer->cdata().data());
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
        if (channel == "orders") {
          process_orders_pushed_data(data_iter->second.get<json::array_t>());
        } else if (channel == "balance_and_position") {
          process_pushed_balance_data(data_iter->second.get<json::array_t>());
        }
      }
    }
    if (!m_accountsSubscribedTo) {
      m_accountsSubscribedTo = true;
      return subscribe_to_accounts_channel();
    }
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
  wait_for_messages();
}

void okex_stream_t::process_orders_pushed_data(json::array_t const &data_list) {
  for (auto const &data_json : data_list) {
    auto const data_object = data_json.get<json::object_t>();
    // SWAP, FUTURES, SPOT
    okex::ws_order_info_t order_info{};
    order_info.instrumentType =
        data_object.at("instType").get<json::string_t>();
    // BTC-USDT, DOGE-USDT
    order_info.instrumentID = data_object.at("instId").get<json::string_t>();
    order_info.currency = data_object.at("ccy").get<json::string_t>();
    order_info.orderID = data_object.at("ordId").get<json::string_t>();
    order_info.orderPrice = data_object.at("px").get<json::string_t>();
    order_info.quantityPurchased = data_object.at("sz").get<json::string_t>();
    order_info.orderType = data_object.at("ordType").get<json::string_t>();
    order_info.orderSide = data_object.at("side").get<json::string_t>();
    order_info.positionSide = data_object.at("posSide").get<json::string_t>();
    // isolated, cash, cross
    order_info.tradeMode = data_object.at("tdMode").get<json::string_t>();
    order_info.lastFilledQuantity =
        data_object.at("fillSz").get<json::string_t>();
    order_info.lastFilledFee = data_object.at("fillFee").get<json::string_t>();
    order_info.lastFilledCurrency =
        data_object.at("fillFeeCcy").get<json::string_t>();
    order_info.state = data_object.at("state").get<json::string_t>();
    order_info.feeCurrency = data_object.at("feeCcy").get<json::string_t>();
    order_info.fee = data_object.at("fee").get<json::string_t>();

    auto const updated_time = utils::okexMsTimeToString(
        data_object.at("uTime").get<json::string_t>());
    auto const created_time = utils::okexMsTimeToString(
        data_object.at("cTime").get<json::string_t>());
    order_info.updatedTime = updated_time.has_value() ? *updated_time : "";
    order_info.createdTime = created_time.has_value() ? *created_time : "";

    // -1 for failure, 0 for success, 1 for automatic cancel
    order_info.amendResult =
        data_object.at("amendResult").get<json::string_t>();
    order_info.amendErrorMessage = data_object.at("msg").get<json::string_t>();
    // order_info.for_aliased_account = host_info_->account_alias;
    // order_info.telegram_group = host_info_->tg_group_name;

    m_streamResult.append(order_info);
  }
}

void okex_stream_t::process_pushed_balance_data(
    json::array_t const &data_list) {
  for (auto const &json_data : data_list) {
    json::object_t const data_item = json_data.get<json::object_t>();
    auto balance_data_iter = data_item.find("balData");
    if (balance_data_iter == data_item.cend())
      continue;

    auto const balance_data_list =
        balance_data_iter->second.get<json::array_t>();
    okex::ws_balance_data_t balance;
    for (auto const &json_balance : balance_data_list) {
      auto const item = json_balance.get<json::object_t>();
      balance.currency = item.at("ccy").get<json::string_t>();
      balance.balance = item.at("cashBal").get<json::string_t>();
      m_streamResult.append(balance);
    }
  }
}

void addOkexAccountStream(std::vector<std::shared_ptr<okex_stream_t>> &streams,
                          account_info_t const &accountInfo,
                          net::io_context &ioContext,
                          ssl::context &sslContext) {}

void removeOkexAccountStream(
    std::vector<std::shared_ptr<okex_stream_t>> &streams,
    account_info_t const &accountInfo) {}

} // namespace keep_my_journal
