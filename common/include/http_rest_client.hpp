#pragma once

#include "http_client.hpp"

#include <boost/asio/deadline_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <queue>

namespace keep_my_journal {
class http_rest_client_t {
  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

  net::io_context &m_ioContext;

  char const *const m_hostApi;
  char const *const m_service;
  int m_retries = 0;
  std::string const m_target;

  std::map<std::string, std::string> m_optHeader;
  std::optional<beast::flat_buffer> m_buffer = std::nullopt;
  std::optional<resolver> m_resolver = std::nullopt;
  std::optional<http::request<http::string_body>> m_httpRequest = std::nullopt;
  std::optional<http::response<http::string_body>> m_httpResponse =
      std::nullopt;
  std::optional<signed_message_t> m_signedAuth = std::nullopt;
  std::optional<beast::tcp_stream> m_tcpStream = std::nullopt;
  std::optional<net::deadline_timer> m_timer = std::nullopt;
  std::queue<std::string> m_payloads;
  error_callback_t m_errorCallback;
  success_callback_t m_successCallback;

  void rest_api_prepare_request();
  void rest_api_send_request();
  void rest_api_receive_response();
  void rest_api_on_data_received(beast::error_code);
  void rest_api_initiate_connection();
  void rest_api_connect_to_resolved_names(results_type const &);
  void sign_request();
  void goto_temporary_sleep();
  void report_error_and_retry(boost::system::error_code);
  void send_next_payload() { rest_api_send_request(); }
  void rest_api_perform_action() { rest_api_send_request(); }

public:
  http_rest_client_t(net::io_context &, char const *host, char const *service,
                     std::string target);

  void insert_header(std::string const &key, std::string const &value);
  void add_payload(std::string const &payload);
  void set_callbacks(error_callback_t &&errorCallback,
                     success_callback_t &&successCallback);
  void send_data();
};

} // namespace keep_my_journal