#include "http_rest_client.hpp"

#include "crypto_utils.hpp"
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <utility>

namespace keep_my_journal {

http_rest_client_t::http_rest_client_t(net::io_context &ioContext,
                                       char const *const hostApi,
                                       char const *const service,
                                       std::string target)
    : m_ioContext(ioContext), m_hostApi(hostApi), m_service(service),
      m_target(std::move(target)) {}

void http_rest_client_t::rest_api_initiate_connection() {
  m_resolver.emplace(m_ioContext);
  m_resolver->async_resolve(
      m_hostApi, m_service,
      [this](auto const error_code,
             net::ip::tcp::resolver::results_type const &results) {
        if (error_code)
          return report_error_and_retry(error_code);
        rest_api_connect_to_resolved_names(results);
      });
}

void http_rest_client_t::report_error_and_retry(
    boost::system::error_code const ec) {
  if (m_errorCallback)
    m_errorCallback(ec);
  if (++m_retries < 5)
    return rest_api_initiate_connection();
}

void http_rest_client_t::rest_api_connect_to_resolved_names(
    results_type const &resolved_names) {
  m_resolver.reset();

  m_tcpStream.emplace(m_ioContext);
  m_tcpStream->expires_after(std::chrono::seconds(30));
  m_tcpStream->async_connect(resolved_names,
                             [this](auto const error_code, auto const &) {
                               if (error_code)
                                 return report_error_and_retry(error_code);
                               rest_api_perform_action();
                             });
}

void http_rest_client_t::rest_api_prepare_request() {
  using http::field;
  using http::verb;

  auto &request = m_httpRequest.emplace();
  request.version(11);
  request.target(m_target);
  request.set(field::host, m_hostApi);
  request.set(field::user_agent, "MyCryptoLog/0.0.1");
  request.set(field::accept, "*/*");
  request.set(field::accept_language, "en-US,en;q=0.5 --compressed");

  request.method(verb::post);
  request.body() = m_payloads.front();

  // the message requires signing
  if (m_signedAuth)
    sign_request();

  request.prepare_payload();
}

void http_rest_client_t::sign_request() {
  assert(m_signedAuth.has_value());

  auto &request = *m_httpRequest;
  auto set_header =
      [&request](signed_message_t::header_value_t const &s) mutable {
        if (!(s.key.empty() && s.value.empty()))
          request.set(s.key, s.value);
      };

  signed_message_t &auth = *m_signedAuth;
  set_header(auth.apiKey);
  set_header(auth.timestamp);
  set_header(auth.apiVersion);

  std::string const stringToSign =
      auth.timestamp.value + "POST" + m_target +
      (m_payloads.empty() ? std::string{} : m_payloads.front());
  std::string const signature = utils::base64Encode(
      utils::hmac256Encode(stringToSign, auth.secretKey.value));
  std::string const passPhrase = utils::base64Encode(
      utils::hmac256Encode(auth.passPhrase.value, auth.secretKey.value));
  request.set(auth.passPhrase.key, passPhrase);
  request.set(auth.secretKey.key, signature);
}

void http_rest_client_t::rest_api_send_request() {
  if (m_payloads.empty())
    return goto_temporary_sleep();

  if (!m_tcpStream->socket().is_open())
    return rest_api_initiate_connection();

  rest_api_prepare_request();
  m_tcpStream->expires_after(std::chrono::seconds(20));
  http::async_write(*m_tcpStream, *m_httpRequest,
                    [this](beast::error_code const ec, std::size_t const) {
                      if (ec)
                        return report_error_and_retry(ec);
                      rest_api_receive_response();
                    });
}

void http_rest_client_t::send_data() {
  if (!m_tcpStream.has_value()) // it has not started running
    rest_api_initiate_connection();
}

void http_rest_client_t::goto_temporary_sleep() {
  if (!m_timer)
    m_timer.emplace(m_ioContext);

  boost::system::error_code ec{};
  m_timer->cancel(ec); // cancel whatever async op was scheduled

  m_timer->expires_from_now(boost::posix_time::milliseconds(500));
  m_timer->async_wait(
      [this](boost::system::error_code const ec) { rest_api_send_request(); });
}

void http_rest_client_t::rest_api_receive_response() {
  m_httpRequest.reset();
  m_buffer.emplace();
  m_httpResponse.emplace();

  m_tcpStream->expires_after(std::chrono::seconds(20));
  http::async_read(*m_tcpStream, *m_buffer, *m_httpResponse,
                   [this](beast::error_code ec, std::size_t const sz) {
                     rest_api_on_data_received(ec);
                   });
}

void http_rest_client_t::rest_api_on_data_received(beast::error_code const ec) {
  if (ec && m_errorCallback)
    return m_errorCallback(ec);

  if (m_successCallback)
    m_successCallback(m_httpResponse->body());
  m_payloads.pop();

  m_retries = 0;
  send_next_payload();
}

} // namespace keep_my_journal
