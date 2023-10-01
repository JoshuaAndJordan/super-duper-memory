#include "session.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>

#include <jwt/jwt.hpp>
#include <price_stream/commodity.hpp>
#include <account_stream/user_scheduled_task.hpp>

#include "crypto_utils.hpp"
#include "database_connector.hpp"
#include "enumerations.hpp"
#include "json_utils.hpp"
#include "random_utils.hpp"
#include "string_utils.hpp"
#include "user_info.hpp"

extern std::string BEARER_TOKEN_SECRET_KEY;

namespace jordan {
  std::optional<account_monitor_task_result_t>
  queue_monitoring_task(account_scheduled_task_t const &task);

enum constant_e { RequestBodySize = 1'024 * 1'024 * 50 };

std::string get_alphanum_tablename(std::string str) {
  static auto non_alphanum_remover = [](char const ch) {
    return !std::isalnum(ch);
  };
  str.erase(std::remove_if(str.begin(), str.end(), non_alphanum_remover),
            str.end());
  return str;
}

std::filesystem::path const download_path =
    std::filesystem::current_path() / "downloads" / "zip_files";

void endpoint_t::add_endpoint(std::string const &route,
                              std::initializer_list<http::verb> const &verbs,
                              callback_t &&callback) {
  if (route.empty() || route[0] != '/')
    throw std::runtime_error{"A valid route starts with a /"};
  endpoints.emplace(route, rule_t{verbs, std::move(callback)});
}

std::optional<endpoint_t::rule_iterator>
endpoint_t::get_rules(std::string const &target) {
  auto iter = endpoints.find(target);
  if (iter == endpoints.end()) {
    return std::nullopt;
  }
  return iter;
}

std::string task_state_to_string(task_state_e const state) {
  switch (state) {
  case task_state_e::initiated:
    return "initiated";
  case task_state_e::remove:
    return "removed";
  case task_state_e::restarted:
    return "restarted";
  case task_state_e::running:
    return "running";
  case task_state_e::stopped:
    return "stopped";
  case task_state_e::unknown:
  default:
    return "unknown";
  }
}

std::optional<endpoint_t::rule_iterator>
endpoint_t::get_rules(boost::string_view const &target) {
  return get_rules(target.to_string());
}

void to_json(json &j, instrument_type_t const &instr) {
  j = json{{"name", instr.name}, {"price", instr.currentPrice},
           {"open24hr", instr.open24h},
           {"type", utils::tradeTypeToString(instr.tradeType) }
  };
}

/*
void to_json(json &j, api_key_data_t const &data) {
  j = json{{"key", data.key}, {"alias", data.alias_for_account}};
}

void to_json(json &j, task_result_t const &item) {
  j = json{{"symbol", item.symbol},
           {"market_price", item.marketPrice},
           {"order_price", item.orderPrice},
           {"quantity", item.quantity},
           {"pnl", item.pnl},
           {"task_type", item.taskType},
           {"col_id", item.columnID}};
}

void to_json(json &j, user_task_t const &item) {
  j = json{{"symbol", item.tokenName},     {"side", item.direction},
           {"time", item.monitorTimeSecs}, {"money", item.money},
           {"price", item.orderPrice},     {"quantity", item.quantity},
           {"task_type", item.taskType},   {"column_id", item.columnID}};
}
*/

std::unordered_map<std::string, session_metadata_t>
    session_t::m_bearerTokenMap{};

session_t::session_t(net::io_context &io, net::ip::tcp::socket &&socket)
    : m_ioContext{io}, m_tcpStream{std::move(socket)} {}

std::shared_ptr<session_t> session_t::add_endpoint_interfaces() {
  using http::verb;

  m_endpointApis.add_endpoint("/", {verb::get},
                              ROUTE_CALLBACK(index_page_handler));
  m_endpointApis.add_endpoint("/register", {verb::post},
                              JSON_ROUTE_CALLBACK(register_new_user));
  m_endpointApis.add_endpoint("/login", {verb::post},
                              JSON_ROUTE_CALLBACK(user_login_handler));
  m_endpointApis.add_endpoint("/pricing_task", {verb::post},
                              JSON_AUTH_ROUTE_CALLBACK(add_new_pricing_tasks));
  m_endpointApis.add_endpoint("/price", {verb::post},
                              JSON_AUTH_ROUTE_CALLBACK(get_price_handler));
  m_endpointApis.add_endpoint("/get_file", {verb::get},
                              JSON_AUTH_ROUTE_CALLBACK(get_file_handler));
  m_endpointApis.add_endpoint("/trading_pairs", {verb::get},
                              AUTH_ROUTE_CALLBACK(get_trading_pairs_handler));
  m_endpointApis.add_endpoint("/create_task", {verb::get},
                              AUTH_ROUTE_CALLBACK(get_user_jobs_handler));
  m_endpointApis.add_endpoint("/monitor_user_account", {verb::post},
                              JSON_AUTH_ROUTE_CALLBACK(monitor_user_account));
  return shared_from_this();
}

void session_t::http_write(
    beast::tcp_stream &tcpStream, file_serializer_t &fileSerializer,
    std::function<void()> func)
{
  http::async_write(tcpStream, fileSerializer,
                    [callback = std::move(func), self = shared_from_this()](
                        beast::error_code ec, std::size_t const size_written) {
                      self->m_fileSerializer.reset();
                      self->m_fileResponse.reset();
                      callback();
                      self->on_data_written(ec, size_written);
                    });
}

void session_t::run() { http_read_data(); }

void session_t::http_read_data() {
  m_buffer.clear();
  m_emptyBodyParser.emplace();
  m_emptyBodyParser->body_limit(RequestBodySize);
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::minutes(5));
  http::async_read_header(m_tcpStream, m_buffer, *m_emptyBodyParser,
                          ASYNC_CALLBACK(on_header_read));
}

void session_t::on_header_read(beast::error_code ec, std::size_t const) {
  if (ec == http::error::end_of_stream)
    return shutdown_socket();

  if (ec) {
    return error_handler(
        server_error(ec.message(), error_type_e::ServerError, {}), true);
  } else {
    m_contentType = m_emptyBodyParser->get()[http::field::content_type];
    m_clientRequest = std::make_unique<http::request_parser<http::string_body>>(
        std::move(*m_emptyBodyParser));
    http::async_read(m_tcpStream, m_buffer, *m_clientRequest,
                     ASYNC_CALLBACK(on_data_read));
  }
}

void session_t::handle_requests(string_request_t const &request) {
  std::string const request_target{utils::decodeUrl(request.target())};

  if (request_target.empty())
    return index_page_handler(request, {});

  auto const method = request.method();
  boost::string_view request_target_view = request_target;
  auto split = utils::splitStringView(request_target_view, "?");
  if (auto iter = m_endpointApis.get_rules(split[0]); iter.has_value()) {
    auto const iter_end = iter.value()->second.verbs.cend();
    auto const found_iter =
        std::find(iter.value()->second.verbs.cbegin(), iter_end, method);
    if (found_iter == iter_end) {
      return error_handler(method_not_allowed(request));
    }
    boost::string_view const query_string = split.size() > 1 ? split[1] : "";
    auto url_query_{split_optional_queries(query_string)};
    return iter.value()->second.routeCallback(request, url_query_);
  }
  return error_handler(not_found(request));
}

void session_t::on_data_read(beast::error_code const ec, std::size_t const) {
  if (ec) {
    if (ec == http::error::end_of_stream) // end of connection
      return shutdown_socket();
    else if (ec == http::error::body_limit) {
      return error_handler(server_error(ec.message(), error_type_e::ServerError,
                                        string_request_t{}),
                           true);
    }
    return error_handler(server_error(ec.message(), error_type_e::ServerError,
                                      string_request_t{}), true);
  }

  return handle_requests(m_clientRequest->get());
}

bool session_t::is_closed() {
  return !beast::get_lowest_layer(m_tcpStream).socket().is_open();
}

void session_t::shutdown_socket() {
  beast::error_code ec{};
  (void)beast::get_lowest_layer(m_tcpStream)
      .socket()
      .shutdown(net::socket_base::shutdown_send, ec);
  ec = {};
  (void)beast::get_lowest_layer(m_tcpStream).socket().close(ec);
  beast::get_lowest_layer(m_tcpStream).close();
}

void session_t::error_handler(string_response_t &&response, bool close_socket) {
  auto resp = std::make_shared<string_response_t>(std::move(response));
  m_resp = resp;
  if (!close_socket) {
    http::async_write(m_tcpStream, *resp, ASYNC_CALLBACK(on_data_written));
  } else {
    http::async_write(
        m_tcpStream, *resp,
        [self = shared_from_this()](auto const err_c, std::size_t const) {
          self->shutdown_socket();
        });
  }
}

void session_t::on_data_written(beast::error_code ec,
                                [[maybe_unused]] std::size_t const bytes_written) {
  if (ec)
    return spdlog::error(ec.message());

  m_resp = nullptr;
  http_read_data();
}

void session_t::index_page_handler(string_request_t const &request,
                                   url_query_t const &) {
  return error_handler(
      get_error("login", error_type_e::NoError, http::status::ok, request));
}

void session_t::register_new_user(string_request_t const &request,
                                  [[maybe_unused]] url_query_t const &query) {
  using utils::get_object_member;
  jordan::user_registration_data_t registrationData;

  try {
    auto const json_object = json::parse(request.body()).get<json::object_t>();
    get_object_member(json_object, "username", registrationData.username);
    get_object_member(json_object, "email", registrationData.email);
    get_object_member(json_object, "first_name", registrationData.firstName);
    get_object_member(json_object, "last_name", registrationData.lastName);
    get_object_member(json_object, "address", registrationData.address);
    get_object_member(json_object, "password_hash",
                      registrationData.passwordHash);
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("unexpected JSON content", request));
  }
  auto &db_connector = database_connector_t::s_get_db_connector();
  if (db_connector->username_exists(registrationData.username))
    return error_handler(get_error("username already exists",
                                   error_type_e::ServerError, http::status::ok,
                                   request));

  if (db_connector->email_exists(registrationData.email))
    return error_handler(get_error("email already exists",
                                   error_type_e::ServerError, http::status::ok,
                                   request));

  if (!db_connector->add_new_user(registrationData)) {
    return error_handler(get_error("there was an error trying to register user",
                                   error_type_e::ServerError, http::status::ok,
                                   request));
  }

  return error_handler(
      json_success("account registered successfully", request));
}

void session_t::user_login_handler(string_request_t const &request,
                                   url_query_t const &) {
  using utils::get_json_value;

  auto &body = request.body();
  try {
    json json_root = json::parse(std::string_view(body.data(), body.size()));
    json::object_t const login_object = json_root.get<json::object_t>();
    auto const username =
        get_json_value<json::string_t>(login_object, "username");
    auto const password_hash =
        get_json_value<json::string_t>(login_object, "password_hash");
    auto &database_connector = database_connector_t::s_get_db_connector();
    if (!database_connector->is_valid_user(username, password_hash)) {
      return error_handler(get_error("invalid username or password",
                                     error_type_e::Unauthorized,
                                     http::status::unauthorized, request));
    }

    auto const currentTime = std::time(nullptr);
    auto const token =
        generate_bearer_token(username, currentTime, BEARER_TOKEN_SECRET_KEY);
    m_bearerTokenMap[token] = session_metadata_t{username, currentTime};

    json::object_t result_obj;
    result_obj["status"] = error_type_e::NoError;
    result_obj["message"] = "success";
    result_obj["token"] = token;

    return send_response(json_success(result_obj, request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("json object not valid", request));
  }
}

std::string session_t::generate_bearer_token(std::string const &username,
                                             time_t const current_time,
                                             std::string const &secret_key) {
  using jwt::params::algorithm;
  using jwt::params::payload;
  using jwt::params::secret;

  std::unordered_map<std::string, std::string> info_result;
  info_result["hash_used"] = "HS256";
  info_result["login_time"] = std::to_string(current_time);
  info_result["random"] = utils::getRandomString(utils::getRandomInteger());
  info_result["username"] = username;

  jwt::jwt_object obj{algorithm("HS256"), payload(info_result),
                      secret(secret_key)};
  return obj.signature();
}

std::optional<json::object_t>
decode_bearer_token(std::string const &token, std::string const &secret_key) {
  using jwt::params::algorithms;
  using jwt::params::secret;

  try {
    auto dec_obj =
        jwt::decode(token, algorithms({"HS256"}), secret(secret_key));
    return dec_obj.payload().create_json_obj().get<json::object_t>();
  } catch (std::exception const &) {
    return std::nullopt;
  }
}


  void session_t::get_file_handler(string_request_t const &request,
                                 url_query_t const &optional_query) {
  auto const id_iter = optional_query.find("id");
  if (id_iter == optional_query.cend())
    return error_handler(bad_request("key parameter missing", request));

  std::string file_path{};
  try {
    file_path = utils::base64Decode(id_iter->second.to_string());
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
  if (file_path.empty()) {
    return error_handler(not_found(request));
  }
  char const *const content_type =
      "application/zip, application/octet-stream, "
      "application/x-zip-compressed, multipart/x-zip";
  auto callback = [file_path] {
    std::error_code temp_ec{};
    std::filesystem::remove(file_path, temp_ec);
  };
  return send_file(file_path, content_type, request, callback);
}

void session_t::get_trading_pairs_handler(string_request_t const &request,
                                          url_query_t const &optional_query) {
  auto const exchange_iter = optional_query.find("exchange");
  if (exchange_iter == optional_query.end()) {
    return error_handler(bad_request("query `exchange` missing", request));
  }

  auto const exchange = utils::stringToExchange(
      boost::to_lower_copy(exchange_iter->second.to_string()));

  auto const names = instrument_sink_t::get_unique_instruments(exchange)
      .to_list();
  return send_response(json_success(names, request));
}

void session_t::monitor_user_account(string_request_t const &request, url_query_t const &) {
  try {
    auto const jsonRoot = json::parse(request.body()).get<json::object_t>();
    auto const exchangeIter = jsonRoot.find("exchange");
    auto const secretKeyIter = jsonRoot.find("secret_key");
    auto const passphraseIter = jsonRoot.find("passphrase");
    auto const apiKeyIter = jsonRoot.find("api_key");
    auto const userIDIter = jsonRoot.find("user_id");
    auto const tradeTypeIter = jsonRoot.find("trade_type");
    if (!utils::anyOf(jsonRoot, tradeTypeIter, exchangeIter,
                      userIDIter, secretKeyIter, passphraseIter, apiKeyIter))
      throw std::runtime_error("exchange/secret_key/passphrase missing");

    account_scheduled_task_t task {};
    task.exchange = utils::stringToExchange(
        exchangeIter->second.get<json::string_t>());
    task.tradeType = utils::stringToTradeType(
        tradeTypeIter->second.get<json::string_t>());

    if (task.exchange == exchange_e::total)
      return error_handler(bad_request("Invalid exchange specified", request));

    // trade type is only necessary for a kucoin account
    if (task.exchange == exchange_e::kucoin && task.tradeType == trade_type_e::total)
    {
      return error_handler(bad_request("Invalid trade type specified for kucoin data", request));
    }

    task.userID = userIDIter->second.get<json::number_integer_t>();
    task.passphrase = passphraseIter->second.get<json::string_t>();
    task.secretKey = secretKeyIter->second.get<json::string_t >();
    task.apiKey = apiKeyIter->second.get<json::string_t>();
    task.operation = task_operation_e::add;

    auto& databaseConnector = database_connector_t::s_get_db_connector();
    task.taskID = databaseConnector->add_new_monitor_task(task);
    if (task.taskID < 0) {
      auto const errorMessage = "an error prevented this task from being added to the DB";
      return error_handler(bad_request(errorMessage, request));
    }

    auto const optResult = queue_monitoring_task(task);
    if (!optResult.has_value()) {
      if (!databaseConnector->remove_monitor_task(task.userID, task.taskID)) {
        spdlog::error("I was unable to remove the task with ID {} and user {}",
                      task.taskID, task.userID);
      }
      return error_handler(server_error("there was a problem scheduling this task",
                                        error_type_e::ServerError, request));
    }

    json::object_t jsonObject;
    jsonObject["task_id"] = task.taskID;
    jsonObject["state"] = (int)optResult->state;

    return send_response(json_success(jsonObject, request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", request));
  }
}

void session_t::scheduled_price_job_handler(string_request_t const &request,
                                            url_query_t const &optional_query) {
  if (!is_validated_user(request)) {
    return error_handler(permission_denied(request));
  }
  if (!is_json_request()) {
    return error_handler(bad_request("invalid content-type", request));
  }
  auto const action_iter = optional_query.find("action");
  if (action_iter == optional_query.end()) {
    return error_handler(bad_request("query `action` missing", request));
  }

  auto const action = boost::to_lower_copy(action_iter->second.to_string());

  if (action == "remove" || action == "delete") {
    return stop_scheduled_jobs(request, task_state_e::remove);
  } else if (action == "stop") {
    return stop_scheduled_jobs(request, task_state_e::stopped);
  } else if (action == "restart") {
    return restart_scheduled_jobs(request);
  } else if (action == "result") {
    return get_tasks_result(request);
  }

  return error_handler(bad_request("unknown 'action' specified", request));
}

void session_t::add_new_pricing_tasks(string_request_t const &request,
                                      url_query_t const &query) {
  auto &scheduled_job_list = request_handler_t::get_all_scheduled_tasks();

  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    std::string global_request_id{};
    if (auto const request_id_iter = json_root.find("id");
        json_root.end() != request_id_iter) {
      global_request_id = request_id_iter->second.get<json::string_t>();
    }

    std::size_t const current_time = std::time(nullptr);
    auto const json_job_list = json_root.at("contracts").get<json::array_t>();
    for (auto const &json_item : json_job_list) {
      auto const json_object = json_item.get<json::object_t>();
      scheduled_task_t new_task{};
      new_task.symbol =
          boost::to_upper_copy(json_object.at("symbol").get<json::string_t>());
      new_task.columnID =
          json_object.at("column_id").get<json::number_unsigned_t>();
      new_task.direction =
          boost::to_lower_copy(json_object.at("side").get<json::string_t>());
      new_task.monitorTimeSecs = static_cast<int>(
          json_object.at("time").get<json::number_integer_t>());
      new_task.orderPrice = json_object.at("price").get<json::number_float_t>();
      new_task.quantity = json_object.at("qty").get<json::number_float_t>();
      new_task.money = json_object.at("money").get<json::number_float_t>();

      auto const task_type =
          json_object.at("task_type").get<json::number_integer_t>();
      if (task_type > 1) { // task_type == 1, price changes only
        return error_handler(bad_request("unknown `task_type` found", request));
      }

      new_task.forUsername = m_currentUsername;
      new_task.currentTime = current_time;
      new_task.status = task_state_e::initiated;
      new_task.taskType = static_cast<task_type_e>(task_type);

      if (auto request_id_iter = json_object.find("id");
          request_id_iter != json_object.end()) {
        new_task.requestID = request_id_iter->second.get<json::string_t>();
      } else {
        if (global_request_id.empty())
          global_request_id = utils::getRandomString(10);

        new_task.requestID = global_request_id;
      }
      scheduled_job_list.append(std::move(new_task));
    }

    json::object_t result;
    result["status"] = static_cast<int>(error_type_e::NoError);
    result["message"] = "OK";
    if (!global_request_id.empty()) {
      result["id"] = global_request_id;
    }
    return send_response(json_success(result, request));

  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", request));
  }
}

void session_t::restart_scheduled_jobs(string_request_t const &request) {
  auto &scheduled_job_list = request_handler_t::get_all_scheduled_tasks();
  try {
    json::array_t const request_id_list =
        json::parse(request.body()).get<json::array_t>();
    for (auto const &json_item : request_id_list) {
      scheduled_task_t task{};
      task.forUsername = m_currentUsername;
      task.requestID = json_item.get<json::string_t>();
      task.status = task_state_e::restarted;
      scheduled_job_list.append(std::move(task));
    }
    json::object_t result;
    result["status"] = static_cast<int>(error_type_e::NoError);
    result["message"] = "OK";
    return send_response(json_success(result, request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", request));
  }
}

void session_t::stop_scheduled_jobs(string_request_t const &request,
                                    task_state_e const status) {
  auto &scheduled_job_list = request_handler_t::get_all_scheduled_tasks();
  try {
    json::array_t const request_id_list =
        json::parse(request.body()).get<json::array_t>();
    for (auto const &json_item : request_id_list) {
      scheduled_task_t task{};
      task.forUsername = m_currentUsername;
      task.requestID = json_item.get<json::string_t>();
      task.status = status;
      scheduled_job_list.append(std::move(task));
    }
    json::object_t result;
    result["status"] = static_cast<int>(error_type_e::NoError);
    result["message"] = "OK";
    return send_response(json_success(result, request));

  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", request));
  }
}

void session_t::get_tasks_result(string_request_t const &request) {
  auto const records_table_name =
      get_alphanum_tablename(m_currentUsername) + "_records";
  auto &database_connector = database_connector_t::s_get_db_connector();

  std::unordered_map<std::string,
                     std::map<std::string, std::vector<task_result_t>>>
      result_map{};

  try {

    auto const request_list = json::parse(request.body()).get<json::array_t>();
    for (auto const &json_item : request_list) {
      auto const item_object = json_item.get<json::object_t>();
      auto const request_id = item_object.at("id").get<json::string_t>();
      std::string begin_time{};
      std::string end_time{};
      if (auto const begin_time_iter = item_object.find("begin_time");
          begin_time_iter != item_object.end()) {
        begin_time = begin_time_iter->second.get<json::string_t>();
      }
      if (auto const end_time_iter = item_object.find("end_time");
          end_time_iter != item_object.end()) {
        end_time = end_time_iter->second.get<json::string_t>();
      }
      auto task_result = database_connector->get_task_result(
          records_table_name, request_id, begin_time, end_time);
      auto &request_data = result_map[request_id];
      for (auto &item : task_result) {
        request_data[item.current_time].push_back(std::move(item));
      }
    }
    return send_response(json_success(std::move(result_map), request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", request));
  }
}

bool session_t::is_json_request() const {
  return boost::iequals(m_contentType, "application/json");
}

void session_t::get_price_handler(string_request_t const &request,
                                  url_query_t const &) {
  json::array_t result;
  auto const &tokens = request_handler_t::get_all_pushed_data();
  try {
    auto const object_root = json::parse(request.body()).get<json::object_t>();
    auto const contracts = object_root.at("contracts").get<json::array_t>();
    for (auto const &json_token : contracts) {
      auto const token_name =
          boost::to_upper_copy(json_token.get<json::string_t>());
      if (auto const find_iter = tokens.find(token_name);
          find_iter != tokens.cend()) {
        auto const &data = find_iter->second;
        auto const change =
            ((data.currentPrice - data.open24h) / data.open24h) * 100.0;

        json::object_t item;
        item["name"] = data.symbolID;
        item["price"] = data.currentPrice;
        item["open_24h"] = data.open24h;
        item["change"] = change;
        result.push_back(std::move(item));
      }
    }
    return send_response(json_success(result, request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", request));
  }
}

bool session_t::extract_bearer_token(string_request_t const &request,
                                     std::string &token) {
  token.clear();
  std::string const authorization_str = request[http::field::authorization];
  if (!authorization_str.empty()) {
    auto const bearer_start_string = "Bearer ";
    auto const index = authorization_str.find(bearer_start_string);
    if (index == 0) {
      auto const bearer_start_len = std::strlen(bearer_start_string);
      token = authorization_str.substr(index + bearer_start_len);
    }
  }
  return !token.empty();
}

bool session_t::is_validated_user(string_request_t const &request) {
  std::string token{};
  if (!extract_bearer_token(request, token))
    return false;
  auto iter = m_bearerTokenMap.find(token);
  bool const found = iter != m_bearerTokenMap.end();
  if (found)
    m_currentUsername = iter->second.username;
  return found;
}

void session_t::get_user_jobs_handler(string_request_t const &request,
                                      url_query_t const &) {
  static std::vector<task_state_e> const statuses {
      task_state_e::initiated,
      task_state_e::running,
      task_state_e::stopped
  };
  auto &database_connector = database_connector_t::s_get_db_connector();
  auto task_list =
      database_connector->get_users_tasks(statuses, m_currentUsername);
  std::map<std::string, decltype(task_list)> task_map{};
  for (auto &task : task_list) {
    task_map[task.requestID].push_back(std::move(task));
  }

  json::array_t result_list;
  for (auto const &[task_id, contracts] : task_map) {
    if (contracts.empty())
      continue;

    auto &first_contract = contracts[0];
    json::object_t item;
    item["task_id"] = task_id;
    item["status"] = task_state_to_string(first_contract.status);
    item["create_time"] = first_contract.createdTime;
    item["last_begin_time"] = first_contract.lastBeginTime;
    item["last_end_time"] = first_contract.lastEndTime;
    item["contracts"] = contracts;
    result_list.push_back(std::move(item));
  }
  return send_response(json_success(std::move(result_list), request));
}

// =========================STATIC FUNCTIONS==============================

string_response_t session_t::not_found(string_request_t const &request) {
  return get_error("url not found", error_type_e::ResourceNotFound,
                   http::status::not_found, request);
}

string_response_t session_t::upgrade_required(string_request_t const &request) {
  return get_error("you need to upgrade your client software",
                   error_type_e::ResourceNotFound,
                   http::status::upgrade_required, request);
}

string_response_t session_t::server_error(std::string const &message,
                                          error_type_e type,
                                          string_request_t const &request) {
  return get_error(message, type, http::status::internal_server_error, request);
}

string_response_t session_t::bad_request(std::string const &message,
                                         string_request_t const &request) {
  return get_error(message, error_type_e::BadRequest, http::status::bad_request,
                   request);
}

string_response_t
session_t::permission_denied(string_request_t const &request) {
  return get_error("permission denied", error_type_e::Unauthorized,
                   http::status::unauthorized, request);
}

string_response_t session_t::method_not_allowed(string_request_t const &req) {
  return get_error("method not allowed", error_type_e::MethodNotAllowed,
                   http::status::method_not_allowed, req);
}

string_response_t session_t::get_error(std::string const &error_message,
                                       error_type_e type, http::status status,
                                       string_request_t const &req) {
  json::object_t result_obj;
  result_obj["status"] = type;
  result_obj["message"] = error_message;
  json result = result_obj;

  string_response_t response{status, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

string_response_t session_t::json_success(json const &body,
                                          string_request_t const &req) {
  string_response_t response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = body.dump();
  response.prepare_payload();
  return response;
}

string_response_t session_t::success(char const *message,
                                     string_request_t const &req) {
  json::object_t result_obj;
  result_obj["status"] = error_type_e::NoError;
  result_obj["message"] = message;
  json result(result_obj);

  string_response_t response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

void session_t::send_response(string_response_t &&response) {
  auto resp = std::make_shared<string_response_t>(std::move(response));
  m_resp = resp;
  http::async_write(m_tcpStream, *resp,
                    beast::bind_front_handler(&session_t::on_data_written,
                                              shared_from_this()));
}

url_query_t
session_t::split_optional_queries(boost::string_view const &optional_query) {
  url_query_t result{};
  if (!optional_query.empty()) {
    auto queries = utils::splitStringView(optional_query, "&");
    for (auto const &q : queries) {
      auto split = utils::splitStringView(q, "=");
      if (split.size() < 2)
        continue;
      result.emplace(split[0], split[1]);
    }
  }
  return result;
}

} // namespace jordan
