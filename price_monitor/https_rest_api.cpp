#include "https_rest_api.hpp"

#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

namespace jordan {

https_rest_api_t::https_rest_api_t(
    net::io_context &ioContext, net::ssl::context &sslContext,
    beast::ssl_stream<beast::tcp_stream> &sslStream, resolver &resolver,
    char const *const hostApi, char const *const service,
    std::string const &target)
    : m_ioContext(ioContext), m_sslContext(sslContext), m_sslStream(sslStream),
      m_resolver(resolver), m_hostApi(hostApi), m_service(service),
      m_target(target) {}

void https_rest_api_t::rest_api_initiate_connection() {
  m_resolver.async_resolve(
      m_hostApi, m_service,
      [this](auto const error_code,
             net::ip::tcp::resolver::results_type const &results) {
        if (error_code)
          return m_errorCallback(error_code);
        rest_api_connect_to_resolved_names(results);
      });
}

void https_rest_api_t::rest_api_connect_to_resolved_names(
    results_type const &resolved_names) {

  beast::get_lowest_layer(m_sslStream).expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(m_sslStream)
      .async_connect(resolved_names,
                     [this](auto const error_code, auto const &connected_name) {
                       if (error_code)
                         return m_errorCallback(error_code);
                       rest_api_perform_ssl_handshake(connected_name);
                     });
}

void https_rest_api_t::rest_api_perform_ssl_handshake(
    results_type::endpoint_type const &ep) {
  beast::get_lowest_layer(m_sslStream).expires_after(std::chrono::seconds(15));
  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(m_sslStream.native_handle(), m_hostApi)) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    return m_errorCallback(ec);
  }

  m_sslStream.async_handshake(net::ssl::stream_base::client,
                              [this](beast::error_code const ec) {
                                if (ec)
                                  return m_errorCallback(ec);
                                return rest_api_get_all_available_instruments();
                              });
}

void https_rest_api_t::rest_api_get_all_available_instruments() {
  rest_api_prepare_request();
  rest_api_send_request();
}

void https_rest_api_t::rest_api_prepare_request() {
  using http::field;
  using http::verb;

  auto &request = m_httpRequest.emplace();
  request.version(11);
  request.target(m_target);
  request.set(field::host, m_hostApi);
  request.set(field::user_agent, "MyCryptoLog/0.0.1");
  request.set(field::accept, "*/*");
  request.set(field::accept_language, "en-US,en;q=0.5 --compressed");

  if (m_method == http_method_e::get) {
    request.method(verb::get);
  } else if (m_method == http_method_e::post) {
    request.method(verb::post);
    if (m_payload.has_value())
      request.body() = *m_payload;
  }
  request.prepare_payload();
}

void https_rest_api_t::rest_api_send_request() {
  beast::get_lowest_layer(m_sslStream).expires_after(std::chrono::seconds(20));
  http::async_write(m_sslStream, *m_httpRequest,
                    [this](beast::error_code const ec, std::size_t const) {
                      if (ec) {
                        return m_errorCallback(ec);
                      }
                      rest_api_receive_response();
                    });
}

void https_rest_api_t::rest_api_receive_response() {
  m_httpRequest.reset();
  m_buffer.emplace();
  m_httpResponse.emplace();

  beast::get_lowest_layer(m_sslStream).expires_after(std::chrono::seconds(20));
  http::async_read(m_sslStream, *m_buffer, *m_httpResponse,
                   [this](beast::error_code ec, std::size_t const sz) {
                     rest_api_on_data_received(ec);
                   });
}

void https_rest_api_t::rest_api_on_data_received(beast::error_code const ec) {
  if (ec)
    return m_errorCallback(ec);
  m_successCallback(m_httpResponse->body());
}

} // namespace jordan
