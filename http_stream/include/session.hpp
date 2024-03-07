// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/vector_body.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>

#include "endpoint.hpp"

#define ROUTE_CALLBACK(callback)                                               \
  [self = shared_from_this()] BN_REQUEST_PARAM {                               \
    self->callback(optional_query);                                            \
  }

#define JSON_ROUTE_CALLBACK(callback)                                          \
  [self = shared_from_this()] BN_REQUEST_PARAM {                               \
    if (!self->is_json_request())                                              \
      return self->error_handler(                                              \
          bad_request("invalid content-type", self->m_thisRequest));           \
    self->callback(optional_query);                                            \
  }

#define ASYNC_CALLBACK(callback)                                               \
  [self = shared_from_this()](auto const a, auto const b) {                    \
    self->callback(a, b);                                                      \
  }

namespace keep_my_journal {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using string_response_t = http::response<http::string_body>;
using string_request_t = http::request<http::string_body>;

enum class error_type_e {
  NoError,
  ResourceNotFound,
  RequiresUpdate,
  BadRequest,
  ServerError,
  MethodNotAllowed,
  Unauthorized
};

// defined in subscription_data.hpp
enum class task_state_e : std::size_t;

class session_t : public std::enable_shared_from_this<session_t> {
  using vector_body_t = http::vector_body<unsigned char>;

private:
  net::io_context &m_ioContext;
  endpoint_t m_endpoints;
  beast::flat_buffer m_buffer{};
  std::optional<http::response<vector_body_t>> m_bufferResponse = std::nullopt;
  std::shared_ptr<void> m_cachedResponse = nullptr;
  std::optional<http::request_parser<http::string_body>> m_clientRequest =
      std::nullopt;
  beast::tcp_stream m_tcpStream;
  string_request_t m_thisRequest{};
  std::string m_contentType{};

private:
  void http_read_data();
  void on_data_read(beast::error_code ec, std::size_t);
  void shutdown_socket();
  void send_response(string_response_t &&response);
  void error_handler(string_response_t &&response, bool close_socket = false);
  void on_data_written(beast::error_code ec, std::size_t bytes_written);
  void handle_requests(string_request_t const &request);
  void get_trading_pairs_handler(url_query_t const &optional_query);
  void add_new_pricing_tasks(url_query_t const &);
  void latest_price_handler(url_query_t const &);
  void monitor_user_account(url_query_t const &);
  void get_prices_task_status(url_query_t const &);
  void stop_prices_task(url_query_t const &);
  void get_all_running_price_tasks(url_query_t const &);
  bool is_json_request() const;

public:
  session_t(net::io_context &io, net::ip::tcp::socket &&socket);
  std::shared_ptr<session_t> add_endpoint_interfaces();
  void run();
};
} // namespace keep_my_journal
