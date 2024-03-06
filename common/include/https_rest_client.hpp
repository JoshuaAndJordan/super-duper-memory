// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include "http_client.hpp"
#include <boost/beast/ssl.hpp>

namespace keep_my_journal {
class https_rest_api_t {
  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

  net::io_context &m_ioContext;
  net::ssl::context &m_sslContext;
  beast::ssl_stream<beast::tcp_stream> &m_sslStream;
  resolver &m_resolver;
  char const *const m_hostApi;
  char const *const m_service;
  std::string const m_target;

  std::optional<std::string> m_payload = std::nullopt;
  std::map<std::string, std::string> m_optHeader;
  http_method_e m_method = http_method_e::get;

  std::optional<beast::flat_buffer> m_buffer;
  std::optional<http::request<http::string_body>> m_httpRequest;
  std::optional<http::response<http::string_body>> m_httpResponse;
  std::optional<signed_message_t> m_signedAuth = std::nullopt;
  error_callback_t m_errorCallback;
  success_callback_t m_successCallback;

  void rest_api_prepare_request();
  void rest_api_get_all_available_instruments();
  void rest_api_send_request();
  void rest_api_receive_response();
  void rest_api_on_data_received(beast::error_code const);
  void rest_api_initiate_connection();
  void rest_api_connect_to_resolved_names(results_type const &);
  void rest_api_perform_ssl_handshake(results_type::endpoint_type const &);
  void sign_request();

public:
  https_rest_api_t(net::io_context &, net::ssl::context &,
                   beast::ssl_stream<beast::tcp_stream> &m_sslStream,
                   resolver &resolver, char const *const host,
                   char const *const service, std::string const &target);

  void set_method(http_method_e);
  void insert_header(std::string const &key, std::string const &value);
  void set_payload(std::string const &payload);
  void install_auth(signed_message_t const &msg) { m_signedAuth.emplace(msg); }
  void run() { rest_api_initiate_connection(); }
  void set_callbacks(error_callback_t &&errorCallback,
                     success_callback_t &&successCallback);
};
} // namespace keep_my_journal
