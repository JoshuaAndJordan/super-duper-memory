// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/string_body.hpp>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>

#include "fields_alloc.hpp"

#define BN_REQUEST_PARAM                                                       \
  (string_request_t const &request, url_query_t const &optional_query)

#define JSON_ROUTE_CALLBACK(callback)                                          \
  [self = shared_from_this()] BN_REQUEST_PARAM {                               \
    if (!self->is_json_request())                                              \
      return self->error_handler(                                              \
          bad_request("invalid content-type", request));                       \
    self->callback(request, optional_query);                                   \
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
using url_query_t = std::map<boost::string_view, boost::string_view>;
using string_body_ptr =
    std::unique_ptr<http::request_parser<http::string_body>>;
using alloc_t = fields_alloc<char>;
using nlohmann::json;

using callback_t =
    std::function<void(string_request_t const &, url_query_t const &)>;

struct rule_t {
  std::vector<http::verb> verbs{};
  callback_t routeCallback;

  rule_t(std::initializer_list<http::verb> const &verbs, callback_t callback)
      : verbs(verbs), routeCallback{std::move(callback)} {}
};

class endpoint_t {
  std::map<std::string, rule_t> endpoints;
  using rule_iterator = std::map<std::string, rule_t>::iterator;

public:
  void add_endpoint(std::string const &,
                    std::initializer_list<http::verb> const &, callback_t &&);
  std::optional<rule_iterator> get_rules(std::string const &target);
  std::optional<rule_iterator> get_rules(boost::string_view const &target);
};

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
  using file_serializer_t =
      http::response_serializer<http::file_body, http::basic_fields<alloc_t>>;

  net::io_context &m_ioContext;
  beast::tcp_stream m_tcpStream;
  beast::flat_buffer m_buffer{};
  std::optional<http::request_parser<http::empty_body>> m_emptyBodyParser{};
  string_body_ptr m_clientRequest{};
  boost::string_view m_contentType{};
  std::shared_ptr<void> m_resp;
  endpoint_t m_endpointApis;
  std::optional<http::response<http::file_body, http::basic_fields<alloc_t>>>
      m_fileResponse;
  alloc_t m_fileAlloc{8 * 1'024};
  // The file-based response serializer.
  std::optional<file_serializer_t> m_fileSerializer = std::nullopt;

private:
  void http_read_data();
  void on_header_read(beast::error_code, std::size_t);
  void on_data_read(beast::error_code ec, std::size_t);
  void shutdown_socket();
  void send_response(string_response_t &&response);
  void error_handler(string_response_t &&response, bool close_socket = false);
  void on_data_written(beast::error_code ec, std::size_t bytes_written);
  void handle_requests(string_request_t const &request);
  void get_trading_pairs_handler(string_request_t const &request,
                                 url_query_t const &optional_query);
  void add_new_pricing_tasks(string_request_t const &, url_query_t const &);
  void latest_price_handler(string_request_t const &, url_query_t const &);
  void monitor_user_account(string_request_t const &, url_query_t const &);
  void get_prices_task_status(string_request_t const &, url_query_t const &);
  void stop_prices_task(string_request_t const &, url_query_t const &);
  void get_all_running_price_tasks(string_request_t const &,
                                   url_query_t const &);
  bool is_json_request() const;

private:
  static string_response_t json_success(json const &body,
                                        string_request_t const &req);
  static string_response_t success(char const *message,
                                   string_request_t const &);
  static string_response_t bad_request(std::string const &message,
                                       string_request_t const &);
  static string_response_t not_found(string_request_t const &);
  static string_response_t method_not_allowed(string_request_t const &request);
  static string_response_t server_error(std::string const &, error_type_e,
                                        string_request_t const &);
  static string_response_t get_error(std::string const &, error_type_e,
                                     http::status, string_request_t const &);
  static url_query_t split_optional_queries(boost::string_view const &args);

public:
  session_t(net::io_context &io, net::ip::tcp::socket &&socket);
  std::shared_ptr<session_t> add_endpoint_interfaces();
  bool is_closed();
  void run();
};
} // namespace keep_my_journal
