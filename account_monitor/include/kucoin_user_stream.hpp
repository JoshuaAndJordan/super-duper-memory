#pragma once

#include <boost/asio/deadline_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include "account_stream/user_scheduled_task.hpp"
#include "enumerations.hpp"
#include "json_utils.hpp"
#include "uri.hpp"
#include <optional>
#include <set>

namespace boost::asio {
namespace ssl {
class context;
}
class io_context;
} // namespace boost::asio

namespace jordan {
namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace ws = beast::websocket;
namespace ip = net::ip;

class https_rest_api_t;

// kucoin user stream websocket
class kucoin_ua_stream_t
    : public std::enable_shared_from_this<kucoin_ua_stream_t> {
  struct instance_server_data_t {
    std::string endpoint; // wss://foo.com/path
    int pingIntervalMs = 0;
    [[maybe_unused]] int pingTimeoutMs = 0;
    int encryptProtocol = 0; // bool encrypt or not
  };

  enum class subscription_stage_e {
    none,
    private_order_change_v2,
    account_balance_change,
    stop_order_event,
    nothing_left,
  };

  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

  net::io_context &m_ioContext;
  ssl::context &m_sslContext;
  std::optional<resolver> m_resolver = std::nullopt;
  std::optional<ws::stream<beast::ssl_stream<beast::tcp_stream>>>
      m_sslWebStream = std::nullopt;
  std::optional<beast::flat_buffer> m_readWriteBuffer = std::nullopt;
  std::optional<net::deadline_timer> m_pingTimer;
  std::unique_ptr<https_rest_api_t> m_httpClient = nullptr;
  std::string m_apiHost;
  std::string m_apiService;
  std::string m_requestToken;
  std::string m_subscriptionString;
  std::vector<instance_server_data_t> m_instanceServers;
  uri_t m_uri;
  account_info_t const m_accountInfo;
  trade_type_e const m_tradeType;
  subscription_stage_e m_stage = subscription_stage_e::none;

public:
  kucoin_ua_stream_t(net::io_context &ioContext, ssl::context &sslContext,
                     account_info_t const &info, trade_type_e const);
  virtual ~kucoin_ua_stream_t() = default;
  void run();
  void stop();

protected:
  virtual void reset_counter() { m_stage = subscription_stage_e::none; }
  virtual std::string rest_api_host() const = 0;
  virtual std::string rest_api_service() const = 0;
  virtual std::string get_private_order_change_json() = 0;
  virtual std::string get_account_balance_change_json() = 0;
  virtual std::string get_stop_order_event_json() = 0;

  friend void removeKucoinAccountStream(
      std::vector<std::shared_ptr<kucoin_ua_stream_t>>& list,
      account_info_t const & info);
private:
  void rest_api_obtain_token();
  void start_ping_timer();
  void reset_ping_timer();
  void on_ping_timer_tick(boost::system::error_code const &);
  void report_error_and_retry(beast::error_code const);
  void negotiate_websocket_connection();
  void initiate_websocket_connection();
  void websocket_perform_ssl_handshake();
  void websocket_connect_to_resolved_names(results_type const &);
  void perform_websocket_handshake();
  void wait_for_messages();
  void send_next_subscription();
  void interpret_generic_messages();
  void on_token_obtained(std::string const &token);
};

class kucoin_futures_ua_stream_t : public kucoin_ua_stream_t {
public:
  kucoin_futures_ua_stream_t(net::io_context &, ssl::context &,
                             account_info_t const &);
  std::string rest_api_host() const override {
    return "api-futures.kucoin.com";
  }
  std::string rest_api_service() const override { return "https"; }
  std::string get_private_order_change_json() override;
  std::string get_account_balance_change_json() override;
  std::string get_stop_order_event_json() override;
};

class kucoin_spot_ua_stream_t : public kucoin_ua_stream_t {
public:
  kucoin_spot_ua_stream_t(net::io_context &, ssl::context &,
                          account_info_t const &);
  std::string rest_api_host() const override { return "api.kucoin.com"; }
  std::string rest_api_service() const override { return "https"; }
  std::string get_private_order_change_json() override;
  std::string get_account_balance_change_json() override;
  std::string get_stop_order_event_json() override;
};

} // namespace jordan
