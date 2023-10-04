// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#pragma once

#include <boost/asio/high_resolution_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <memory>
#include <optional>

#include "account_stream/okex_order_info.hpp"
#include "account_stream/user_scheduled_task.hpp"
#include "json_utils.hpp"

namespace keep_my_journal {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace ssl = net::ssl;
namespace websocket = beast::websocket;
namespace ip = net::ip;

class okex_stream_t : public std::enable_shared_from_this<okex_stream_t> {
  static char const *const ws_api_host;
  static char const *const ws_api_service;

  net::io_context &m_ioContext;
  net::ssl::context &m_sslContext;
  std::optional<net::ip::tcp::resolver> m_resolver = std::nullopt;
  std::optional<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>
      m_sslWebStream = std::nullopt;
  std::optional<account_info_t> m_accountInfo = std::nullopt;
  std::optional<std::string> m_sendingBufferText = std::nullopt;
  std::optional<beast::flat_buffer> m_buffer = std::nullopt;
  std::optional<net::high_resolution_timer> m_timer = std::nullopt;
  okex::okex_container_t &m_streamResult;
  bool m_stopped = false;
  bool m_accountsSubscribedTo = false;

  using resolver_results_t = ip::tcp::resolver::results_type;

private:
  void initiate_websocket_connection();
  void connect_to_resolved_names(resolver_results_t const &);
  void perform_ssl_handshake(resolver_results_t::endpoint_type const &);
  void perform_websocket_handshake();
  void perform_user_login();
  void read_login_response();
  void interpret_login_response();
  void subscribe_to_orders_channels();
  void on_instruments_subscribed();
  void wait_for_messages();
  void interpret_generic_messages();
  void process_orders_pushed_data(json::array_t const &);
  void on_ws_connection_severed(std::string const &errorString);
  void subscribe_to_accounts_channel();
  void process_pushed_balance_data(json::array_t const &);

public:
  okex_stream_t(net::io_context &, net::ssl::context &, account_info_t &&);
  void run();
  void stop();
};
} // namespace keep_my_journal
