#include "userstream_keyalive.hpp"

#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <spdlog/spdlog.h>

namespace jordan {

char const *const userstream_keyalive_t::host_name = "api.binance.com";

userstream_keyalive_t::userstream_keyalive_t(net::io_context &ioContext,
                                             net::ssl::context &sslContext,
                                             std::string listenKey,
                                             std::string apiKey)
    : m_ioContext(ioContext), m_sslContext(sslContext),
      m_listenKey(std::move(listenKey)), m_apiKey(std::move(apiKey)) {}

userstream_keyalive_t::~userstream_keyalive_t() {
  m_buffer.reset();
  m_httpRequest.reset();
  m_httpResponse.reset();
  m_resolver.reset();
  m_sslStream.reset();
}

void userstream_keyalive_t::run() { resolve_name(); }

void userstream_keyalive_t::resolve_name() {
  spdlog::info("Running keepalive_t to keep it alive...");

  m_resolver = std::make_unique<resolver>(m_ioContext);
  m_resolver->async_resolve(
      host_name, "https",
      [self = shared_from_this()](auto const error_code,
                                  resolver::results_type const &results) {
        if (error_code)
          return spdlog::error(error_code.message());
        self->connect_to_names(results);
      });
}

void userstream_keyalive_t::connect_to_names(
    resolver::results_type const &resolved_names) {
  m_resolver.reset();
  m_sslStream = std::make_unique<beast::ssl_stream<beast::tcp_stream>>(
      m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_sslStream).expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(*m_sslStream)
      .async_connect(resolved_names,
                     [self = shared_from_this()](
                         beast::error_code const error_code,
                         resolver::results_type::endpoint_type const &ip) {
                       if (error_code)
                         return spdlog::error(error_code.message());
                       self->perform_ssl_connection(ip);
                     });
}

void userstream_keyalive_t::perform_ssl_connection(
    resolver::results_type::endpoint_type const &connected_name) {
  auto const host = fmt::format("{}:{}", host_name, connected_name.port());

  // Set a timeout on the operation
  beast::get_lowest_layer(*m_sslStream).expires_after(std::chrono::seconds(30));

  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(m_sslStream->native_handle(), host.c_str())) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    return spdlog::error(ec.message());
  }
  m_sslStream->async_handshake(
      net::ssl::stream_base::client,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec)
          return spdlog::error(ec.message());
        self->renew_listen_key();
      });
}

void userstream_keyalive_t::renew_listen_key() {
  prepare_request();
  send_request();
}

void userstream_keyalive_t::prepare_request() {
  using http::field;
  using http::verb;

  m_httpRequest = std::make_unique<http::request<http::empty_body>>();
  auto &request = *m_httpRequest;
  request.method(verb::put);
  request.version(11);
  request.target("/api/v3/userDataStream?listenKey=" + m_listenKey);
  request.set(field::host, host_name);
  request.set(field::user_agent, "MyCryptoLog/0.0.1");
  request.set(field::accept, "*/*");
  request.set(field::accept_language, "en-US,en;q=0.5 --compressed");
  request.set("X-MBX-APIKEY", m_apiKey);
}

void userstream_keyalive_t::send_request() {
  beast::get_lowest_layer(*m_sslStream).expires_after(std::chrono::seconds(20));
  http::async_write(*m_sslStream, *m_httpRequest,
                    [self = shared_from_this()](beast::error_code const ec,
                                                std::size_t const) {
                      if (ec)
                        return spdlog::error(ec.message());
                      self->receive_response();
                    });
}

void userstream_keyalive_t::receive_response() {
  m_httpRequest.reset();
  m_buffer = std::make_unique<beast::flat_buffer>();
  m_httpResponse = std::make_unique<http::response<http::string_body>>();

  beast::get_lowest_layer(*m_sslStream).expires_after(std::chrono::seconds(20));
  http::async_read(
      *m_sslStream, *m_buffer, *m_httpResponse,
      [self = shared_from_this()](beast::error_code ec, std::size_t const sz) {
        if (ec)
          return spdlog::error(ec.message());
        self->on_data_received();
      });
}

void userstream_keyalive_t::on_data_received() {
  spdlog::info("[ListenKey]received data: {}", m_httpResponse->body());
  m_httpResponse.reset();
  m_buffer.reset();
}

} // namespace jordan
