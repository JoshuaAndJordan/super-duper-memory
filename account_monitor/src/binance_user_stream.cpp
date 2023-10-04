// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#include "binance_user_stream.hpp"

#include <boost/beast/http/read.hpp>
#include <spdlog/spdlog.h>
#include <utility>

#include "crypto_utils.hpp"
#include "userstream_keyalive.hpp"

namespace keep_my_journal {
char const *const binance_stream_t::rest_api_host = "api.binance.com";
char const *const binance_stream_t::ws_host = "stream.binance.com";
char const *const binance_stream_t::ws_port_number = "9443";

binance_stream_t::binance_stream_t(net::io_context &ioContext,
                                   net::ssl::context &sslContext,
                                   account_info_t userInfo)
    : m_ioContext(ioContext), m_sslContext(sslContext),
      m_results(binance::account_stream_sink_t::get_account_stream()),
      m_userInfo(std::move(userInfo)), m_sslWebStream{}, m_resolver{} {}

binance_stream_t::~binance_stream_t() {
  if (m_sslWebStream)
    m_sslWebStream->close({});

  m_buffer.reset();
  m_onErrorTimer.reset();
  m_listenKeyTimer.reset();
  m_sslWebStream.reset();
}

void binance_stream_t::run() { rest_api_initiate_connection(); }

void binance_stream_t::stop() {
  m_isStopped = true;

  if (m_sslWebStream) {
    m_sslWebStream->async_close(
        websock::close_reason{},
        [](beast::error_code const ec) { spdlog::error(ec.message()); });
  }
}

void binance_stream_t::rest_api_initiate_connection() {
  if (m_isStopped)
    return;

  m_listenKeyTimer.reset();
  m_onErrorTimer.reset();
  auto onError = [](beast::error_code const &errorCode) {
    spdlog::error(errorCode.message());
  };

  auto onSuccess = [self = shared_from_this()](std::string const &data) {
    self->rest_api_on_data_received(data);
  };

  m_resolver = std::make_unique<resolver>(m_ioContext);
  m_sslWebStream =
      std::make_unique<ssl_websocket_stream_t>(m_ioContext, m_sslContext);

  m_httpClient = std::make_unique<https_rest_api_t>(
      m_ioContext, m_sslContext, m_sslWebStream->next_layer(), *m_resolver,
      rest_api_host, "https", "/api/v3/userDataStream");
  m_httpClient->insert_header("X-MBX-APIKEY", m_userInfo.apiKey);
  m_httpClient->set_method(http_method_e::post);
  m_httpClient->set_callbacks(std::move(onError), std::move(onSuccess));
  m_httpClient->run();
}

void binance_stream_t::rest_api_on_data_received(std::string const &data) {
  try {
    auto const result = json::parse(data).get<json::object_t>();
    if (auto const listen_key_iter = result.find("listenKey");
        listen_key_iter != result.cend()) {
      m_listenKey = std::make_unique<std::string>(
          listen_key_iter->second.get<json::string_t>());
    } else {
      spdlog::error(data);
      return;
    }
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }

  m_httpClient.reset();
  m_sslWebStream.reset();
  return ws_initiate_connection();
}

void binance_stream_t::ws_initiate_connection() {
  if (m_isStopped || !m_listenKey)
    return;

  m_sslWebStream =
      std::make_unique<ssl_websocket_stream_t>(m_ioContext, m_sslContext);
  m_resolver = std::make_unique<resolver>(m_ioContext);

  m_resolver->async_resolve(
      ws_host, ws_port_number,
      [self = shared_from_this()](
          auto const error_code,
          net::ip::tcp::resolver::results_type const &results) {
        if (error_code) {
          return spdlog::error(error_code.message());
        }
        self->ws_connect_to_names(results);
      });
}

void binance_stream_t::ws_connect_to_names(
    net::ip::tcp::resolver::results_type const &resolved_names) {

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
            self->ws_perform_ssl_handshake(connected_name);
          });
}

void binance_stream_t::ws_perform_ssl_handshake(
    net::ip::tcp::resolver::results_type::endpoint_type const &ep) {
  auto const host = fmt::format("{}:{}", ws_host, ep.port());

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
        return self->ws_upgrade_to_websocket();
      });
}

// this sends an upgrade from HTTPS to ws protocol and thus ws handshake begins
// https://binance-docs.github.io/apidocs/spot/en/#user-data-streams

void binance_stream_t::ws_upgrade_to_websocket() {
  auto const binance_handshake_path = "/ws/" + *m_listenKey;

  auto opt = websock::stream_base::timeout();
  opt.idle_timeout = std::chrono::minutes(5);
  opt.handshake_timeout = std::chrono::seconds(20);

  // enable the automatic keepalive pings
  opt.keep_alive_pings = true;
  m_sslWebStream->set_option(opt);

  m_sslWebStream->control_callback(
      [self = shared_from_this()](auto const frame_type, auto const &) {
        if (frame_type == websock::frame_type::close) {
          if (!self->m_isStopped)
            return self->on_ws_connection_severed();
        } else if (frame_type == websock::frame_type::pong) {
          spdlog::info("pong...");
        }
      });

  m_sslWebStream->async_handshake(
      ws_host, binance_handshake_path,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec)
          return spdlog::error(ec.message());

        if (!self->m_listenKeyTimer)
          self->activate_listen_key_keepalive();
        self->ws_wait_for_messages();
      });
}

void binance_stream_t::ws_wait_for_messages() {
  m_buffer = std::make_unique<beast::flat_buffer>();
  m_sslWebStream->async_read(
      *m_buffer, [self = shared_from_this()](beast::error_code const error_code,
                                             std::size_t const) {
        if (error_code) {
          spdlog::error(error_code.message());
          return self->on_ws_connection_severed();
        }
        self->ws_interpret_generic_messages();
      });
}

void binance_stream_t::ws_interpret_generic_messages() {
  char const *buffer_cstr = static_cast<char const *>(m_buffer->cdata().data());
  std::string_view const buffer(buffer_cstr, m_buffer->size());

  try {
    json::object_t const root = json::parse(buffer).get<json::object_t>();
    if (auto const event_iter = root.find("e"); event_iter != root.cend()) {
      // only three events are expected
      if (auto const event_type = event_iter->second.get<json::string_t>();
          event_type == "executionReport") {
        ws_process_orders_execution_report(root);
      } else if (event_type == "balanceUpdate") {
        ws_process_balance_update(root);
      } else if (event_type == "outboundAccountPosition") {
        ws_process_account_position(root);
      }
    }
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }

  return ws_wait_for_messages();
}

// https://binance-docs.github.io/apidocs/spot/en/#payload-balance-update
void binance_stream_t::ws_process_balance_update(
    json::object_t const &balance_object) {
  using utils::get_json_value;

  binance::ws_balance_info_t balance_data{};
  balance_data.balance = get_json_value<string_t>(balance_object, "d");
  balance_data.instrumentID = get_json_value<string_t>(balance_object, "a");

  balance_data.eventTime = get_json_value<inumber_t>(balance_object, "E");
  balance_data.clearTime = get_json_value<inumber_t>(balance_object, "T");
  balance_data.userID = m_userInfo.userID;
  m_results.append(std::move(balance_data));
}

// https://binance-docs.github.io/apidocs/spot/en/#payload-order-update
void binance_stream_t::ws_process_orders_execution_report(
    json::object_t const &order_object) {
  using utils::get_json_value;

  binance::ws_order_info_t order_info{};
  order_info.instrumentID = get_json_value<string_t>(order_object, "s");
  order_info.orderSide = get_json_value<string_t>(order_object, "S");
  order_info.orderType = get_json_value<string_t>(order_object, "o");
  order_info.timeInForce = get_json_value<string_t>(order_object, "f");
  order_info.quantityPurchased = get_json_value<string_t>(order_object, "q");
  order_info.orderPrice = get_json_value<string_t>(order_object, "p");
  order_info.stopPrice = get_json_value<string_t>(order_object, "P");
  order_info.executionType = get_json_value<string_t>(order_object, "x");
  order_info.orderStatus = get_json_value<string_t>(order_object, "X");
  order_info.rejectReason = get_json_value<string_t>(order_object, "r");
  order_info.lastFilledQuantity = get_json_value<string_t>(order_object, "l");
  order_info.commissionAmount = get_json_value<string_t>(order_object, "n");
  order_info.lastExecutedPrice = get_json_value<string_t>(order_object, "L");
  order_info.cumulativeFilledQuantity =
      get_json_value<string_t>(order_object, "z");

  order_info.orderID =
      fmt::format("{}", get_json_value<inumber_t>(order_object, "i"));
  order_info.tradeID =
      fmt::format("{}", get_json_value<inumber_t>(order_object, "t"));

  if (auto const commission_asset_iter = order_object.find("N");
      commission_asset_iter != order_object.cend()) {

    auto json_commission_asset = commission_asset_iter->second;
    // documentation doesn't specify the type of this data but
    // my best guess is that this type is most likely a string
    if (json_commission_asset.is_string()) {
      order_info.commissionAsset = json_commission_asset.get<string_t>();
    } else if (json_commission_asset.is_number()) {
      order_info.commissionAsset =
          std::to_string(json_commission_asset.get<fnumber_t>());
    }
  }

  order_info.eventTime = get_json_value<inumber_t>(order_object, "E");
  order_info.transactionTime = get_json_value<inumber_t>(order_object, "T");
  order_info.createdTime = get_json_value<inumber_t>(order_object, "O");
  order_info.userID = m_userInfo.userID;

  m_results.append(std::move(order_info));
}

// https://binance-docs.github.io/apidocs/spot/en/#payload-account-update
void binance_stream_t::ws_process_account_position(
    json::object_t const &account_object) {
  using utils::get_json_value;

  binance::ws_account_update_t data{};
  data.userID = m_userInfo.userID;
  data.eventTime = get_json_value<inumber_t>(account_object, "E");
  data.lastAccountUpdate = get_json_value<inumber_t>(account_object, "u");

  auto const balances_array = account_object.at("B").get<json::array_t>();
  std::vector<binance::ws_account_update_t> updates{};
  updates.reserve(balances_array.size());

  for (auto const &json_item : balances_array) {
    auto const asset_item = json_item.get<json::object_t>();
    data.instrumentID = get_json_value<string_t>(asset_item, "a");
    data.freeAmount = get_json_value<string_t>(asset_item, "f");
    data.lockedAmount = get_json_value<string_t>(asset_item, "l");
    updates.push_back(data);
  }

  m_results.append_list(std::move(updates));
}

void binance_stream_t::on_periodic_time_timeout() {
  m_listenKeyTimer->expires_after(std::chrono::minutes(30));
  m_listenKeyTimer->async_wait([self = shared_from_this()](
                                   boost::system::error_code const &ec) {
    if (ec || !self->m_sslWebStream)
      return;

    std::make_shared<userstream_keyalive_t>(
        self->m_ioContext, self->m_sslContext, *self->m_listenKey,
        self->m_userInfo.apiKey)
        ->run();
    self->m_listenKeyTimer->cancel();
    net::post(self->m_ioContext, [self] { self->on_periodic_time_timeout(); });
  });
}

void binance_stream_t::on_ws_connection_severed() {
  if (m_sslWebStream) {
    m_sslWebStream->close({});
    m_sslWebStream.reset();
  }

  m_listenKey.reset();
  m_listenKeyTimer.reset();
  m_buffer.reset();

  m_onErrorTimer = std::make_unique<net::high_resolution_timer>(m_ioContext);
  m_onErrorTimer->expires_after(std::chrono::seconds(10));
  m_onErrorTimer->async_wait([self = shared_from_this()](auto const &ec) {
    if (ec)
      return spdlog::error(ec.message());

    self->rest_api_initiate_connection();
  });
}

void binance_stream_t::activate_listen_key_keepalive() {
  m_listenKeyTimer = std::make_unique<net::high_resolution_timer>(m_ioContext);
  on_periodic_time_timeout();
}

void addBinanceAccountStream(
    std::vector<std::shared_ptr<binance_stream_t>> &list,
    account_info_t const &task, net::io_context &ioContext,
    net::ssl::context &sslContext) {
  auto stream = std::make_shared<binance_stream_t>(ioContext, sslContext, task);
  spdlog::info("Adding binance account stream to list...");
  list.push_back(std::move(stream));
  list.back()->run();
}

void removeBinanceAccountStream(
    std::vector<std::shared_ptr<binance_stream_t>> &list,
    account_info_t const &info) {
  auto iter = std::find_if(list.begin(), list.end(),
                           [&info](std::shared_ptr<binance_stream_t> &s) {
                             return s->m_userInfo == info;
                           });
  if (iter != list.end()) {
    (*iter)->stop();
    list.erase(iter);
  }
}
} // namespace keep_my_journal
