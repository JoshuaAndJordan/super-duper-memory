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

#define ROUTE_CALLBACK(callback)                                               \
  [self = shared_from_this()] BN_REQUEST_PARAM {                               \
    self->callback(request, optional_query);                                   \
  }

#define JSON_ROUTE_CALLBACK(callback)                                          \
  [self = shared_from_this()] BN_REQUEST_PARAM {                               \
    if (!self->is_json_request())                                                    \
      return self->error_handler(bad_request("invalid content-type", request));      \
    self->callback(request, optional_query);                                   \
  }

#define AUTH_ROUTE_CALLBACK(callback)                                     \
  [self = shared_from_this()] BN_REQUEST_PARAM {                               \
    if (!self->is_validated_user(request))                                     \
      return self->error_handler(permission_denied(request));                  \
    self->callback(request, optional_query);                                   \
  }

#define JSON_AUTH_ROUTE_CALLBACK(callback)                                     \
  [self = shared_from_this()] BN_REQUEST_PARAM {                               \
    if (!self->is_json_request())                                              \
      return self->error_handler(bad_request("invalid content-type", request)); \
    if (!self->is_validated_user(request))                                     \
      return self->error_handler(permission_denied(request));                  \
    self->callback(request, optional_query);                                   \
  }

#define ASYNC_CALLBACK(callback)                                               \
  [self = shared_from_this()](auto const a, auto const b) {                    \
    self->callback(a, b);                                                      \
  }

namespace jordan {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using string_response_t = http::response<http::string_body>;
using string_request_t = http::request<http::string_body>;
using dynamic_request_t = http::request_parser<http::string_body>;
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

struct session_metadata_t {
  std::string username{};
  time_t loginTime = 0;
};

class endpoint_t {
  std::map<std::string, rule_t> endpoints;
  using rule_iterator = std::map<std::string, rule_t>::iterator;

public:
  void add_endpoint(std::string const &,
                    std::initializer_list<http::verb> const &,
                    callback_t &&);
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
  std::string m_currentUsername{};
  std::shared_ptr<void> m_resp;
  endpoint_t m_endpointApis;
  std::optional<http::response<http::file_body, http::basic_fields<alloc_t>>>
      m_fileResponse;
  alloc_t m_fileAlloc{8 * 1'024};
  // The file-based response serializer.
  std::optional<file_serializer_t> m_fileSerializer = std::nullopt;
  static std::unordered_map<std::string, session_metadata_t> m_bearerTokenMap;

private:
  void http_read_data();
  void on_header_read(beast::error_code, std::size_t);
  void on_data_read(beast::error_code ec, std::size_t);
  void shutdown_socket();
  void send_response(string_response_t &&response);
  void error_handler(string_response_t &&response, bool close_socket = false);
  void on_data_written(beast::error_code ec, std::size_t bytes_written);
  void handle_requests(string_request_t const &request);
  void index_page_handler(string_request_t const &request,
                          url_query_t const &optional_query);
  void get_file_handler(string_request_t const &request,
                        url_query_t const &optional_query);
  void get_trading_pairs_handler(string_request_t const &request,
                                 url_query_t const &optional_query);
  void get_price_handler(string_request_t const &request,
                         url_query_t const &optional_query);
  void user_login_handler(string_request_t const &request,
                          url_query_t const &optional_query);
  void scheduled_price_job_handler(string_request_t const &request,
                                   url_query_t const &optional_query);
  void get_user_jobs_handler(string_request_t const &request,
                             url_query_t const &optional_query);
  void register_new_user(string_request_t const &request,
                         url_query_t const &optional_query);
  void add_new_pricing_tasks(string_request_t const &, url_query_t const &);
  void monitor_user_account(string_request_t const &, url_query_t const &);
  void stop_scheduled_jobs(string_request_t const &, task_state_e);
  void restart_scheduled_jobs(string_request_t const &);
  void get_tasks_result(string_request_t const &);
  bool is_validated_user(string_request_t const &);
  bool is_json_request() const;
  void http_write(beast::tcp_stream &, file_serializer_t &,
                  std::function<void()>);
  bool extract_bearer_token(string_request_t const &, std::string &);
  static std::string generate_bearer_token(
      std::string const &username, time_t current_time,
      std::string const &secret_key);

private:
  static string_response_t json_success(json const &body,
                                        string_request_t const &req);
  static string_response_t success(char const *message,
                                   string_request_t const &);
  static string_response_t bad_request(std::string const &message,
                                       string_request_t const &);
  static string_response_t permission_denied(string_request_t const &);
  static string_response_t not_found(string_request_t const &);
  static string_response_t upgrade_required(string_request_t const &);
  static string_response_t method_not_allowed(string_request_t const &request);
  static string_response_t server_error(std::string const &, error_type_e,
                                        string_request_t const &);
  static string_response_t get_error(std::string const &, error_type_e,
                                     http::status, string_request_t const &);
  static url_query_t split_optional_queries(boost::string_view const &args);
  template <typename Func>
  void send_file(std::filesystem::path const &, boost::string_view,
                 string_request_t const &, Func &&func);

public:
  session_t(net::io_context &io, net::ip::tcp::socket &&socket);
  std::shared_ptr<session_t> add_endpoint_interfaces();
  bool is_closed();
  void run();
};

template <typename Func>
void session_t::send_file(std::filesystem::path const &file_path,
                          boost::string_view const content_type,
                          string_request_t const &request, Func &&func) {
  std::error_code ec_{};
  if (!std::filesystem::exists(file_path, ec_))
    return error_handler(bad_request("file does not exist", request));

  http::file_body::value_type file;
  beast::error_code ec{};
  file.open(file_path.string().c_str(), beast::file_mode::read, ec);
  if (ec) {
    return error_handler(server_error("unable to open file specified",
                                      error_type_e::ServerError, request));
  }
  m_fileResponse.emplace(std::piecewise_construct, std::make_tuple(),
                         std::make_tuple(m_fileAlloc));
  m_fileResponse->result(http::status::ok);
  m_fileResponse->keep_alive(request.keep_alive());
  m_fileResponse->set(http::field::server, "okex-feed");
  m_fileResponse->set(http::field::content_type, content_type);
  m_fileResponse->body() = std::move(file);
  m_fileResponse->prepare_payload();
  m_fileSerializer.emplace(*m_fileResponse);
  http_write(m_tcpStream, *m_fileSerializer, func);
}

std::optional<json::object_t>
decode_bearer_token(std::string const &token, std::string const &secret_key);
std::string get_alphanum_tablename(std::string);

} // namespace jordan
