// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/ssl.hpp>
#include <map>
#include <optional>

namespace keep_my_journal {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websock = beast::websocket;
namespace http = beast::http;
namespace ip = net::ip;

using error_callback_t = std::function<void(beast::error_code const &)>;
using success_callback_t = std::function<void(std::string const &)>;

enum class http_method_e {
  get,
  post,
  put,
};

struct signed_message_t {

  struct header_value_t {
    std::string key;
    std::string value;
  };
  header_value_t timestamp;
  header_value_t apiKey;
  header_value_t passPhrase;
  header_value_t secretKey;
  header_value_t apiVersion;
};

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

  inline void set_method(http_method_e const method) { m_method = method; }
  inline void insert_header(std::string const &key, std::string const &value) {
    m_optHeader[key] = value;
  }

  inline void install_auth(signed_message_t const &msg) {
    m_signedAuth.emplace(msg);
  }

  void set_payload(std::string const &payload) {
    if (payload.empty())
      return m_payload.reset();
    m_payload.emplace(payload);
  }

  inline void set_callbacks(error_callback_t &&errorCallback,
                            success_callback_t &&successCallback) {
    m_errorCallback = std::move(errorCallback);
    m_successCallback = std::move(successCallback);
  }
  inline void run() { rest_api_initiate_connection(); }
};
} // namespace keep_my_journal
