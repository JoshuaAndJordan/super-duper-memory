#include "kucoin_price_stream.hpp"
#include "https_rest_api.hpp"
#include "random_utils.hpp"
#include <spdlog/spdlog.h>

namespace jordan {

double kucoin_get_coin_price(std::string_view const str, bool const isSpot) {
  json::object_t const jsonObject = json::parse(str);
  auto iter = jsonObject.find("data");
  if (iter == jsonObject.end() || !iter->second.is_object())
    return -1.0;

  auto const dataObject = iter->second.get<json::object_t>();
  if (isSpot) {
    auto const priceIter = dataObject.find("price");
    if (priceIter == dataObject.end() || !priceIter->second.is_string()) {
      assert(false);
      return -1.0;
    }
    return std::stod(priceIter->second.get<json::string_t>());
  } else {
    auto const priceIter = dataObject.find("lastTradePrice");
    if (priceIter == dataObject.end() || !priceIter->second.is_string()) {
      assert(false);
      return -1.0;
    }
    return std::stod(priceIter->second.get<json::string_t>());
  }
}

kucoin_price_stream_t::kucoin_price_stream_t(
    net::io_context &ioContext, ssl::context &sslContext,
    std::set<instrument_type_t> &tradedInstruments,
    trade_type_e const tradeType)
    : m_ioContext(ioContext), m_sslContext(sslContext), m_tradeType(tradeType),
      m_apiHost(rest_api_host()), m_apiService(rest_api_service()),
      m_tradedInstruments(tradedInstruments) {}

void kucoin_price_stream_t::rest_api_initiate_connection() {
  m_resolver.emplace(m_ioContext);
  m_sslWebStream.emplace(m_ioContext, m_sslContext);

  auto onError = [self = shared_from_this()](beast::error_code const ec) {
    spdlog::error("KuCoin -> '{}' gave this error: {}", self->m_tradeType,
                  ec.message());
  };

  auto onSuccess = [self = shared_from_this()](std::string const &data) {
    self->on_instruments_received(data);
    self->rest_api_obtain_token();
  };

  m_httpClient = std::make_unique<https_rest_api_t>(
      m_ioContext, m_sslContext, m_sslWebStream->next_layer(), *m_resolver,
      m_apiHost.c_str(), m_apiService.c_str(), rest_api_target());
  m_httpClient->set_method(http_method_e::get);
  m_httpClient->set_callbacks(std::move(onError), std::move(onSuccess));
  m_httpClient->run();
}

void kucoin_price_stream_t::rest_api_obtain_token() {
  auto onError = [self = shared_from_this()](beast::error_code const ec) {
    spdlog::error("KuCoin -> '{}' gave this error: {}", self->m_tradeType,
                  ec.message());
  };

  auto onSuccess = [self = shared_from_this()](std::string const &data) {
    self->on_token_obtained(data);
    self->initiate_websocket_connection();
  };

  m_httpClient = std::make_unique<https_rest_api_t>(
      m_ioContext, m_sslContext, m_sslWebStream->next_layer(), *m_resolver,
      m_apiHost.c_str(), m_apiService.c_str(), rest_api_token_target());
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
        data.pingIntervalMs = instanceObject.find("pingInterval")
                                  ->second.get<json::number_integer_t>();

        data.pingTimeoutMs = instanceObject.find("pingTimeout")
                                 ->second.get<json::number_integer_t>();
        m_instanceServers.push_back(std::move(data));
      }
    }
    if (!m_instanceServers.empty() && !m_requestToken.empty())
      initiate_websocket_connection();

  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
}

void kucoin_price_stream_t::initiate_websocket_connection() {
  if (m_instanceServers.empty() || m_requestToken.empty())
    return;

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
          return spdlog::error(errorCode.message());
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
                         return spdlog::error(errorCode.message());
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
          return spdlog::error(errorCode);
        start_ping_timer();
        wait_for_messages();
      });
}

void kucoin_price_stream_t::reset_ping_timer() {
  if (m_pingTimer) {
    boost::system::error_code ec;
    m_pingTimer->cancel(ec);
    m_pingTimer.reset();
  }
}

void kucoin_price_stream_t::on_ping_timer_tick(
    boost::system::error_code const &ec) {
  if (ec)
    return spdlog::error(ec);

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
        if (errorCode == net::error::operation_aborted) {
          return spdlog::error(errorCode);
        } else if (errorCode) {
          spdlog::error(errorCode);
          self->m_sslWebStream.reset();
          return self->rest_api_initiate_connection();
        }
        self->interpret_generic_messages();
      });
}

void kucoin_price_stream_t::interpret_generic_messages() {
  char const *bufferCstr =
      static_cast<char const *>(m_readWriteBuffer->cdata().data());
  size_t const dataLength = m_readWriteBuffer->size();
  auto const optPrice =
      kucoin_get_coin_price(std::string_view(bufferCstr, dataLength),
                            m_tradeType == trade_type_e::spot);
  if (optPrice != -1.0)
    m_priceResult = optPrice;

  if (!m_tokensSubscribedFor)
    return makeSubscription();
  return waitForMessages();
}

// ===================================================
kucoin_futures_price_stream_t::kucoin_futures_price_stream_t(
    net::io_context &ioContext, ssl::context &sslContext)
    : kucoin_price_stream_t(ioContext, sslContext,
                            instrument_sink_t::get_all_listed_instruments()
                                [exchange_e::kucoin][trade_type_e::futures],
                            trade_type_e::futures) {}

void kucoin_futures_price_stream_t::on_instruments_received(
    std::string const &str) {
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
    for (auto const &tickerItem : tickers) {
      auto const tickerObject = tickerItem.get<json::object_t>();
      data.name = tickerObject.find("symbol")->second.get<json::string_t>();
      data.current_price = std::stod(
          tickerObject.find("lastTradePrice")->second.get<json::string_t>());
      m_tradedInstruments.insert(data);
    }
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
}

// =====================================================
kucoin_spot_price_stream_t::kucoin_spot_price_stream_t(
    net::io_context &ioContext, ssl::context &sslContext)
    : kucoin_price_stream_t(
          ioContext, sslContext,
          instrument_sink_t::get_all_listed_instruments()[exchange_e::kucoin]
                                                         [trade_type_e::spot],
          trade_type_e::spot) {}

void kucoin_spot_price_stream_t::on_instruments_received(
    std::string const &str) {
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
    auto const tickerIter = dataObject.find("tickers");
    if (tickerIter == dataObject.end() || !tickerIter->second.is_array())
      return;
    auto const tickers = tickerIter->second.get<json::array_t>();
    instrument_type_t data;
    for (auto const &tickerItem : tickers) {
      auto const tickerObject = tickerItem.get<json::object_t>();
      data.name = tickerObject.find("symbol")->second.get<json::string_t>();
      data.current_price =
          std::stod(tickerObject.find("last")->second.get<json::string_t>());
      m_tradedInstruments.insert(data);
    }
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
}
} // namespace jordan
