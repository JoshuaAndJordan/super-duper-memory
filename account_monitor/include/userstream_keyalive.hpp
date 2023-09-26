// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/ssl.hpp>
#include <memory>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace ip = net::ip;

namespace jordan {

/* https://binance-docs.github.io/apidocs/spot/en/#listen-key-spot
 * Keepalive a user data stream to prevent a time out. User data streams will
 * close after 60 minutes. It's recommended to send a ping about every 30
 * minutes.
 */
class userstream_keyalive_t
    : public std::enable_shared_from_this<userstream_keyalive_t> {

  using resolver = ip::tcp::resolver;

  static char const *const host_name;

  net::io_context &m_ioContext;
  net::ssl::context &m_sslContext;
  std::unique_ptr<beast::flat_buffer> m_buffer;
  std::unique_ptr<http::request<http::empty_body>> m_httpRequest;
  std::unique_ptr<http::response<http::string_body>> m_httpResponse;
  std::unique_ptr<net::ip::tcp::resolver> m_resolver;
  std::unique_ptr<beast::ssl_stream<beast::tcp_stream>> m_sslStream;

  std::string m_listenKey;
  std::string m_apiKey;

private:
  void resolve_name();
  void connect_to_names(resolver::results_type const &);
  void perform_ssl_connection(resolver::results_type::endpoint_type const &);
  void renew_listen_key();
  void prepare_request();
  void send_request();
  void receive_response();
  void on_data_received();

public:
  ~userstream_keyalive_t();
  userstream_keyalive_t(net::io_context &, net::ssl::context &,
                        std::string listen_key, std::string api_key);
  void run();
};

} // namespace jordan
