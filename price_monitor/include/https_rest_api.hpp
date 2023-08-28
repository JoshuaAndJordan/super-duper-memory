#pragma once

#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/ssl.hpp>

namespace jordan {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websock = beast::websocket;
namespace http = beast::http;
namespace ip = net::ip;

using error_callback_t = std::function<void(beast::error_code const &)>;
using success_callback_t = std::function<void(std::string const &)>;

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

  std::optional<beast::flat_buffer> m_buffer;
  std::optional<http::request<http::empty_body>> m_httpRequest;
  std::optional<http::response<http::string_body>> m_httpResponse;
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

public:
  https_rest_api_t(net::io_context &, net::ssl::context &,
                   beast::ssl_stream<beast::tcp_stream> &m_sslStream,
                   resolver &resolver, char const *const host,
                   char const *const service, std::string const &target);
  inline void set_callbacks(error_callback_t &&errorCallback,
                            success_callback_t &&successCallback) {
    m_errorCallback = std::move(errorCallback);
    m_successCallback = std::move(successCallback);
  }
  inline void run() { rest_api_initiate_connection(); }
};
} // namespace jordan
