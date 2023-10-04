// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#include "kucoin_user_stream.hpp"

#include "https_rest_api.hpp"
#include "random_utils.hpp"
#include <spdlog/spdlog.h>

namespace keep_my_journal {

kucoin_ua_stream_t::kucoin_ua_stream_t(net::io_context &ioContext,
                                       ssl::context &sslContext,
                                       account_info_t const &info,
                                       trade_type_e const tradeType)
    : m_ioContext(ioContext), m_sslContext(sslContext), m_accountInfo(info),
      m_tradeType(tradeType) {}

void kucoin_ua_stream_t::run() {
  m_apiHost = rest_api_host();
  m_apiService = rest_api_service();
  rest_api_obtain_token();
}

void kucoin_ua_stream_t::stop() {}

void kucoin_ua_stream_t::rest_api_obtain_token() {
  m_resolver.emplace(m_ioContext);
  m_sslWebStream.emplace(m_ioContext, m_sslContext);

  auto onError = [self = shared_from_this()](beast::error_code const ec) {
    spdlog::error("KuCoin -> '{}' gave this error: {}", (int)self->m_tradeType,
                  ec.message());
  };

  auto onSuccess = [self = shared_from_this()](std::string const &data) {
    self->on_token_obtained(data);
    self->initiate_websocket_connection();
  };

  auto const nowTime = std::to_string(std::time(nullptr) * 1'000);
  signed_message_t message;
  message.timestamp = {"KC-API-TIMESTAMP", nowTime};
  message.apiKey = {"KC-API-KEY", m_accountInfo.apiKey};
  message.passPhrase = {"KC-API-PASSPHRASE", m_accountInfo.passphrase};
  message.secretKey = {"KC-API-SIGN", m_accountInfo.secretKey};
  message.apiVersion = {"KC-API-KEY-VERSION", "2"};

  m_httpClient = std::make_unique<https_rest_api_t>(
      m_ioContext, m_sslContext, m_sslWebStream->next_layer(), *m_resolver,
      m_apiHost.c_str(), m_apiService.c_str(), "/api/v1/bullet-private");
  m_httpClient->set_method(http_method_e::post);
  m_httpClient->install_auth(std::move(message));
  m_httpClient->set_callbacks(std::move(onError), std::move(onSuccess));
  m_httpClient->run();
}

void kucoin_ua_stream_t::on_token_obtained(std::string const &str) {
  try {
    auto const rootObject = json::parse(str).get<json::object_t>();
    auto const codeIter = rootObject.find("code");
    if (codeIter == rootObject.end() || !codeIter->second.is_string() ||
        codeIter->second.get<json::string_t>() != "200000")
      return;
    auto const dataIter = rootObject.find("data");
    if (dataIter == rootObject.end() || !dataIter->second.is_object())
      return;
    auto const dataObject = dataIter->second.get<json::object_t>();
    auto const tokenIter = dataObject.find("token");
    if (tokenIter == dataObject.end() || !tokenIter->second.is_string())
      return;
    m_requestToken = tokenIter->second.get<json::string_t>();

    auto const serverInstances =
        dataObject.find("instanceServers")->second.get<json::array_t>();
    m_instanceServers.clear();
    m_instanceServers.reserve(serverInstances.size());

    for (auto const &instanceJson : serverInstances) {
      auto const &instanceObject = instanceJson.get<json::object_t>();
      if (auto iter = instanceObject.find("protocol");
          iter != instanceObject.end() && iter->second.is_string() &&
          iter->second.get<json::string_t>() == std::string("websocket")) {
        instance_server_data_t data;
        data.endpoint =
            instanceObject.find("endpoint")->second.get<json::string_t>();
        data.encryptProtocol =
            (int)instanceObject.find("encrypt")->second.get<json::boolean_t>();
        data.pingIntervalMs = (int)instanceObject.find("pingInterval")
                                  ->second.get<json::number_integer_t>();

        data.pingTimeoutMs = (int)instanceObject.find("pingTimeout")
                                 ->second.get<json::number_integer_t>();
        m_instanceServers.push_back(std::move(data));
      }
    }
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
}

void kucoin_ua_stream_t::initiate_websocket_connection() {
  if (m_instanceServers.empty() || m_requestToken.empty()) {
    return spdlog::error("ws instanceServers(size): {}, requestToken: {}",
                         m_instanceServers.size(), m_requestToken);
  }

  // remove all server instances that do not support HTTPS
  m_instanceServers.erase(std::remove_if(m_instanceServers.begin(),
                                         m_instanceServers.end(),
                                         [](instance_server_data_t const &d) {
                                           return d.encryptProtocol == 0;
                                         }),
                          m_instanceServers.end());

  if (m_instanceServers.empty())
    return spdlog::error("No server instance found that supports encryption");

  m_uri = uri_t(m_instanceServers.back().endpoint);
  auto const service = m_uri.protocol() != "wss" ? m_uri.protocol() : "443";
  m_resolver.emplace(m_ioContext);
  m_resolver->async_resolve(
      m_uri.host(), service,
      [self = shared_from_this()](auto const &errorCode,
                                  resolver::results_type const &results) {
        if (errorCode)
          return self->report_error_and_retry(errorCode);
        self->websocket_connect_to_resolved_names(results);
      });
}

void kucoin_ua_stream_t::websocket_connect_to_resolved_names(
    resolver::results_type const &resolvedNames) {
  m_resolver.reset();
  m_sslWebStream.emplace(m_ioContext, m_sslContext);

  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(*m_sslWebStream)
      .async_connect(resolvedNames,
                     [self = shared_from_this()](
                         auto const errorCode,
                         resolver::results_type::endpoint_type const &) {
                       if (errorCode)
                         return self->report_error_and_retry(errorCode);
                       self->websocket_perform_ssl_handshake();
                     });
}

void kucoin_ua_stream_t::websocket_perform_ssl_handshake() {
  auto const host = m_uri.host();
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(10));
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                host.c_str())) {
    auto const errorCode = beast::error_code(
        static_cast<int>(::ERR_get_error()), net::error::get_ssl_category());
    return spdlog::error(errorCode.message());
  }

  negotiate_websocket_connection();
}

void kucoin_ua_stream_t::negotiate_websocket_connection() {
  m_sslWebStream->next_layer().async_handshake(
      ssl::stream_base::client,
      [self = shared_from_this()](beast::error_code const &ec) {
        if (ec) {
          if (ec.category() == net::error::get_ssl_category())
            spdlog::error("SSL Category error");
          return;
        }
        beast::get_lowest_layer(*self->m_sslWebStream).expires_never();
        self->perform_websocket_handshake();
      });
}

void kucoin_ua_stream_t::perform_websocket_handshake() {
  auto const path = m_uri.path() + "?token=" + m_requestToken +
                    "&connectId=" + utils::getRandomString(10);
  m_sslWebStream->async_handshake(
      m_uri.host(), path, [self = shared_from_this()](auto const errorCode) {
        if (errorCode)
          return self->report_error_and_retry(errorCode);
        self->start_ping_timer();
        self->wait_for_messages();
      });
}

void kucoin_ua_stream_t::reset_ping_timer() {
  if (m_pingTimer) {
    boost::system::error_code ec{};
    m_pingTimer->cancel(ec);
    m_pingTimer.reset();
  }
}

void kucoin_ua_stream_t::on_ping_timer_tick(
    boost::system::error_code const &ec) {
  if (ec)
    return spdlog::error(ec.message());

  m_sslWebStream->async_ping({}, [self = shared_from_this()](
                                     boost::system::error_code const &) {
    auto const pingIntervalMs = self->m_instanceServers.back().pingIntervalMs;
    self->m_pingTimer->expires_from_now(
        boost::posix_time::milliseconds(pingIntervalMs));
    self->m_pingTimer->async_wait(
        [self = self->shared_from_this()](
            boost::system::error_code const &errCode) {
          return self->on_ping_timer_tick(errCode);
        });
  });
}

void kucoin_ua_stream_t::start_ping_timer() {
  reset_ping_timer();
  m_pingTimer.emplace(m_ioContext);

  auto const pingIntervalMs = m_instanceServers.back().pingIntervalMs;
  m_pingTimer->expires_from_now(
      boost::posix_time::milliseconds(pingIntervalMs));
  m_pingTimer->async_wait(
      [self = shared_from_this()](boost::system::error_code const &ec) {
        self->on_ping_timer_tick(ec);
      });
}

void kucoin_ua_stream_t::wait_for_messages() {
  m_readWriteBuffer.emplace();
  m_sslWebStream->async_read(
      *m_readWriteBuffer,
      [self = shared_from_this()](beast::error_code const errorCode,
                                  std::size_t const) {
        if (errorCode == net::error::operation_aborted)
          return spdlog::error(errorCode.message());
        else if (errorCode)
          return self->report_error_and_retry(errorCode);
        self->interpret_generic_messages();
      });
}

void kucoin_ua_stream_t::interpret_generic_messages() {
  char const *const bufferCstr =
      static_cast<char const *>(m_readWriteBuffer->cdata().data());
  size_t const dataLength = m_readWriteBuffer->size();
  auto const buffer = std::string_view(bufferCstr, dataLength);
  spdlog::info(buffer);
  if (m_stage != subscription_stage_e::nothing_left)
    return send_next_subscription();

  return wait_for_messages();
}

void kucoin_ua_stream_t::send_next_subscription() {
  if (m_stage == subscription_stage_e::none) {
    m_subscriptionString = get_private_order_change_json();
    m_stage = subscription_stage_e::private_order_change_v2;
  } else if (m_stage == subscription_stage_e::private_order_change_v2) {
    m_subscriptionString = get_account_balance_change_json();
    m_stage = subscription_stage_e::account_balance_change;
  } else if (m_stage == subscription_stage_e::account_balance_change) {
    m_subscriptionString = get_stop_order_event_json();
    m_stage = subscription_stage_e::stop_order_event;
  } else if (m_stage == subscription_stage_e::stop_order_event) {
    m_stage = subscription_stage_e::nothing_left;
    return;
  }

  m_sslWebStream->async_write(
      net::buffer(m_subscriptionString),
      [self = shared_from_this()](auto const errCode, size_t const) {
        self->m_subscriptionString.clear();

        if (errCode)
          return self->report_error_and_retry(errCode);
        self->wait_for_messages();
      });
}

void kucoin_ua_stream_t::report_error_and_retry(beast::error_code const ec) {
  spdlog::error(ec.message());
  reset_ping_timer();
  reset_counter();

  // wait a bit and then retry
  std::this_thread::sleep_for(std::chrono::seconds(5));
  rest_api_obtain_token();
}

// ======================SPOT=============================

std::string get_private_subscription_object(std::string const &topic) {
  json::object_t obj;
  obj["ID"] = utils::getRandomInteger();
  obj["type"] = "subscribe";
  obj["privateChannel"] = true;
  obj["topic"] = topic;

  return json(obj).dump();
}

kucoin_spot_ua_stream_t::kucoin_spot_ua_stream_t(net::io_context &ioContext,
                                                 ssl::context &sslContext,
                                                 account_info_t const &userInfo)
    : kucoin_ua_stream_t(ioContext, sslContext, userInfo, trade_type_e::spot) {}

std::string kucoin_spot_ua_stream_t::get_private_order_change_json() {
  static char const *const topic = "/spotMarket/tradeOrdersV2";
  return get_private_subscription_object(topic);
}

std::string kucoin_spot_ua_stream_t::get_account_balance_change_json() {
  static char const *const topic = "/account/balance";
  return get_private_subscription_object(topic);
}

std::string kucoin_spot_ua_stream_t::get_stop_order_event_json() {
  static char const *const topic = "/spotMarket/advancedOrders";
  return get_private_subscription_object(topic);
}

// ======================FUTURES=============================
kucoin_futures_ua_stream_t::kucoin_futures_ua_stream_t(
    net::io_context &ioContext, ssl::context &sslContext,
    account_info_t const &userInfo)
    : kucoin_ua_stream_t(ioContext, sslContext, userInfo,
                         trade_type_e::futures) {}

std::string kucoin_futures_ua_stream_t::get_private_order_change_json() {
  static char const *const topic = "/contractMarket/tradeOrders";
  return get_private_subscription_object(topic);
}

std::string kucoin_futures_ua_stream_t::get_account_balance_change_json() {
  static char const *const topic = "/contractAccount/wallet";
  return get_private_subscription_object(topic);
}

std::string kucoin_futures_ua_stream_t::get_stop_order_event_json() {
  static char const *const topic = "/contractMarket/advancedOrders";
  return get_private_subscription_object(topic);
}

void addKucoinAccountStream(
    std::vector<std::shared_ptr<kucoin_ua_stream_t>> &list,
    account_info_t const &task, trade_type_e const tradeType,
    net::io_context &ioContext, net::ssl::context &sslContext) {
  std::shared_ptr<kucoin_ua_stream_t> stream = nullptr;
  if (tradeType == trade_type_e::spot) {
    stream =
        std::make_shared<kucoin_spot_ua_stream_t>(ioContext, sslContext, task);
  } else if (tradeType == trade_type_e::futures) {
    stream = std::make_shared<kucoin_futures_ua_stream_t>(ioContext, sslContext,
                                                          task);
  } else
    return;

  spdlog::info("Adding Kucoin account stream to list...");
  list.push_back(std::move(stream));
  list.back()->run();
}

void removeKucoinAccountStream(
    std::vector<std::shared_ptr<kucoin_ua_stream_t>> &list,
    account_info_t const &info) {
  auto iter = std::find_if(list.begin(), list.end(),
                           [&info](std::shared_ptr<kucoin_ua_stream_t> &s) {
                             return s->m_accountInfo == info;
                           });
  spdlog::info("Removing Kucoin account stream to list...");
  if (iter != list.end()) {
    (*iter)->stop();
    list.erase(iter);
  }
}

} // namespace keep_my_journal
