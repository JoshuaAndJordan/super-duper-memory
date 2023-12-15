// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include "kucoin_price_stream.hpp"
#include "https_rest_api.hpp"
#include "random_utils.hpp"
#include <spdlog/spdlog.h>

namespace keep_my_journal {

instrument_type_t get_instrument_from_json(std::string_view const str,
                                           trade_type_e const tradeType) {
  json::object_t const jsonObject = json::parse(str);
  auto dataIter = jsonObject.find("data");
  if (dataIter == jsonObject.end() || !dataIter->second.is_object())
    return {};

  auto const dataObject = dataIter->second.get<json::object_t>();
  instrument_type_t inst;
  inst.tradeType = tradeType;

  if (tradeType == trade_type_e::spot) {
    auto subjectIter = jsonObject.find("subject");
    if (subjectIter == jsonObject.end() || !subjectIter->second.is_string())
      return {};
    inst.name = subjectIter->second.get<json::string_t>();
    auto priceIter = dataObject.find("price");
    if (priceIter == dataObject.end()) {
#ifdef _DEBUG
      assert(false);
#else
      return {};
#endif
    }
    if (priceIter->second.is_string())
      inst.currentPrice = std::stod(priceIter->second.get<json::string_t>());
    else if (priceIter->second.is_number())
      inst.currentPrice = priceIter->second.get<json::number_float_t>();
  } else {
    auto symbolIter = dataObject.find("symbol");
    if (symbolIter == dataObject.end() || !symbolIter->second.is_string())
      return {};
    inst.name = symbolIter->second.get<json::string_t>();
    auto bestBidIter = dataObject.find("bestBidPrice");
    auto bestAskIter = dataObject.find("bestAskPrice");
    if (bestAskIter == dataObject.end() || bestBidIter == dataObject.end() ||
        !(bestBidIter->second.is_string() && bestAskIter->second.is_string())) {
#ifdef _DEBUG
      assert(false);
#else
      return {};
#endif
    }
    auto const bidPrice = std::stod(bestBidIter->second.get<json::string_t>()),
               askPrice = std::stod(bestAskIter->second.get<json::string_t>());
    inst.currentPrice = (bidPrice + askPrice) / 2.0;
  }
  return inst;
}

kucoin_price_stream_t::kucoin_price_stream_t(net::io_context &ioContext,
                                             ssl::context &sslContext,
                                             trade_type_e const tradeType)
    : m_ioContext(ioContext), m_sslContext(sslContext), m_tradeType(tradeType),
      m_tradedInstruments(
          instrument_sink_t::get_all_listed_instruments(exchange_e::kucoin)) {}

void kucoin_price_stream_t::rest_api_initiate_connection() {
  if (!m_tradedInstruments.empty())
    return rest_api_obtain_token();

  m_resolver.emplace(m_ioContext);
  m_sslWebStream.emplace(m_ioContext, m_sslContext);
  m_tokensSubscribedFor = false;

  auto onError = [self = shared_from_this()](beast::error_code const ec) {
    spdlog::error("KuCoin -> '{}' gave this error: {}", (int)self->m_tradeType,
                  ec.message());
  };

  auto onSuccess = [self = shared_from_this()](std::string const &data) {
    self->on_instruments_received(data); // virtual function
    self->rest_api_obtain_token();
  };

  m_httpClient = std::make_unique<https_rest_api_t>(
      m_ioContext, m_sslContext, m_sslWebStream->next_layer(), *m_resolver,
      m_apiHost.c_str(), m_apiService.c_str(), rest_api_target());
  m_httpClient->set_method(http_method_e::get);
  m_httpClient->set_callbacks(std::move(onError), std::move(onSuccess));
  m_httpClient->run();
}

void kucoin_price_stream_t::run() {
  m_apiHost = rest_api_host();
  m_apiService = rest_api_service();
  rest_api_initiate_connection();
}

void kucoin_price_stream_t::rest_api_obtain_token() {
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

  m_httpClient = std::make_unique<https_rest_api_t>(
      m_ioContext, m_sslContext, m_sslWebStream->next_layer(), *m_resolver,
      m_apiHost.c_str(), m_apiService.c_str(), "/api/v1/bullet-public");
  m_httpClient->set_method(http_method_e::post);
  m_httpClient->set_callbacks(std::move(onError), std::move(onSuccess));
  m_httpClient->run();
}

void kucoin_price_stream_t::on_token_obtained(std::string const &str) {
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

void kucoin_price_stream_t::initiate_websocket_connection() {
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

void kucoin_price_stream_t::websocket_connect_to_resolved_names(
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

void kucoin_price_stream_t::websocket_perform_ssl_handshake() {
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

void kucoin_price_stream_t::negotiate_websocket_connection() {
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

void kucoin_price_stream_t::perform_websocket_handshake() {
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

void kucoin_price_stream_t::reset_ping_timer() {
  if (m_pingTimer) {
    boost::system::error_code ec{};
    m_pingTimer->cancel(ec);
    m_pingTimer.reset();
  }
}

void kucoin_price_stream_t::on_ping_timer_tick(
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

void kucoin_price_stream_t::start_ping_timer() {
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

void kucoin_price_stream_t::wait_for_messages() {
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

void kucoin_price_stream_t::interpret_generic_messages() {
  char const *const bufferCstr =
      static_cast<char const *>(m_readWriteBuffer->cdata().data());
  size_t const dataLength = m_readWriteBuffer->size();
  auto const buffer = std::string_view(bufferCstr, dataLength);
  auto const inst =
      keep_my_journal::get_instrument_from_json(buffer, m_tradeType);
  if (!inst.name.empty())
    m_tradedInstruments.append(inst);

  if (!m_tokensSubscribedFor)
    return send_ticker_subscription();

  return wait_for_messages();
}

void kucoin_price_stream_t::send_ticker_subscription() {
  if (m_subscriptionString.empty())
    m_subscriptionString = get_subscription_json();

  m_sslWebStream->async_write(
      net::buffer(m_subscriptionString),
      [self = shared_from_this()](auto const errCode, size_t const) {
        self->m_subscriptionString.clear();

        if (errCode)
          return self->report_error_and_retry(errCode);
        self->wait_for_messages();
      });
}

void kucoin_price_stream_t::report_error_and_retry(beast::error_code const ec) {
  spdlog::error(ec.message());
  m_tradedInstruments.clear();
  reset_ping_timer();
  reset_counter();

  // wait a bit and then retry
  std::this_thread::sleep_for(std::chrono::seconds(5));
  rest_api_initiate_connection();
}

// ===================================================
kucoin_futures_price_stream_t::kucoin_futures_price_stream_t(
    net::io_context &ioContext, ssl::context &sslContext)
    : kucoin_price_stream_t(ioContext, sslContext, trade_type_e::futures),
      m_tokenCounter(0) {}

void kucoin_futures_price_stream_t::reset_counter() {
  m_tokenCounter = m_tokensSubscribedFor = false;
}

void kucoin_futures_price_stream_t::on_instruments_received(
    std::string const &str) {
  m_fInstruments.clear();

  try {
    auto const rootObject = json::parse(str).get<json::object_t>();
    auto const codeIter = rootObject.find("code");
    if (codeIter == rootObject.end() || !codeIter->second.is_string() ||
        codeIter->second.get<json::string_t>() != "200000")
      return;
    auto const dataIter = rootObject.find("data");
    if (dataIter == rootObject.end() || !dataIter->second.is_array())
      return;
    auto const tickers = dataIter->second.get<json::array_t>();

    instrument_type_t data;
    data.tradeType = trade_type_e::futures;
    m_fInstruments.reserve(tickers.size());

    for (auto const &tickerItem : tickers) {
      auto const tickerObject = tickerItem.get<json::object_t>();
      auto const lastPrice = tickerObject.find("lastTradePrice")->second;
      if (lastPrice.is_null())
        continue;

      if (lastPrice.is_number())
        data.currentPrice = lastPrice.get<json::number_float_t>();
      else if (lastPrice.is_string())
        data.currentPrice = std::stod(lastPrice.get<json::string_t>());

      data.name = tickerObject.find("symbol")->second.get<json::string_t>();
      m_fInstruments.push_back(data);
      m_tradedInstruments.append(data);
    }
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
}

std::string kucoin_futures_price_stream_t::get_subscription_json() {
  std::ostringstream ss;
  auto const size = m_fInstruments.size();
  auto const counter = (std::min)(size_t(100), size - m_tokenCounter);

  for (size_t i = 0; i < counter; ++i) {
    ss << m_fInstruments[i + m_tokenCounter].name;
    if (i < (counter - 1))
      ss << ",";
  }

  m_tokenCounter += counter;
  if (m_tokenCounter == size)
    m_tokensSubscribedFor = true;

  json::object_t obj;
  obj["id"] = utils::getRandomInteger();
  obj["type"] = "subscribe";
  obj["topic"] = "/contractMarket/tickerV2:" + ss.str();
  obj["response"] = true;
  return json(obj).dump();
}

// =====================================================
kucoin_spot_price_stream_t::kucoin_spot_price_stream_t(
    net::io_context &ioContext, ssl::context &sslContext)
    : kucoin_price_stream_t(ioContext, sslContext, trade_type_e::spot) {}

void kucoin_spot_price_stream_t::on_instruments_received(
    std::string const &str) {
  auto const rootObject = json::parse(str).get<json::object_t>();
  auto const codeIter = rootObject.find("code");
  if (codeIter == rootObject.end() || !codeIter->second.is_string() ||
      codeIter->second.get<json::string_t>() != "200000")
    return;

  auto const dataIter = rootObject.find("data");
  if (dataIter == rootObject.end() || !dataIter->second.is_object())
    return;
  auto const dataObject = dataIter->second.get<json::object_t>();
  auto const tickerIter = dataObject.find("ticker");
  if (tickerIter == dataObject.end() || !tickerIter->second.is_array())
    return;
  auto const tickers = tickerIter->second.get<json::array_t>();
  instrument_type_t data;
  data.tradeType = trade_type_e::spot;

  for (auto const &tickerItem : tickers) {
    auto const tickerObject = tickerItem.get<json::object_t>();
    auto const temp = tickerObject.find("last")->second;
    if (temp.is_null())
      continue;

    if (temp.is_string())
      data.currentPrice = std::stod(temp.get<json::string_t>());
    else if (temp.is_number())
      data.currentPrice = temp.get<json::number_float_t>();
    else {
      spdlog::error(json(tickerObject).dump());
      throw std::runtime_error("Unknown data sent in on_instruments_received");
    }

    data.name = tickerObject.find("symbol")->second.get<json::string_t>();
    m_tradedInstruments.append(data);
  }
}

std::string kucoin_spot_price_stream_t::get_subscription_json() {
  json::object_t obj;
  obj["id"] = utils::getRandomInteger();
  obj["type"] = "subscribe";
  obj["topic"] = "/market/ticker:all";
  obj["response"] = true;

  m_tokensSubscribedFor = true;
  return json(obj).dump();
}

void kucoin_price_watcher(net::io_context &ioContext,
                          ssl::context &sslContext) {
  auto spotWatcher =
      std::make_shared<kucoin_spot_price_stream_t>(ioContext, sslContext);
  auto futuresWatcher =
      std::make_shared<kucoin_futures_price_stream_t>(ioContext, sslContext);

  spotWatcher->run();
  futuresWatcher->run();
  ioContext.run();
}
} // namespace keep_my_journal
