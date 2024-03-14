// Copyright (C) 2023-2024 Joshua and Jordan Ogunyinka

#include "session.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <spdlog/spdlog.h>

#include <account_stream/user_scheduled_task.hpp>
#include <price_stream/commodity.hpp>

#include "crypto_utils.hpp"
#include "enumerations.hpp"
#include "json_utils.hpp"
#include "scheduled_price_tasks.hpp"
#include "string_utils.hpp"

using keep_my_journal::instrument_exchange_set_t;
extern instrument_exchange_set_t uniqueInstruments;

namespace keep_my_journal {
namespace details {
string_response_t get_error(std::string const &error_message, error_type_e type,
                            http::status status, string_request_t const &req) {
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

string_response_t not_found(string_request_t const &request) {
  return get_error("url not found", error_type_e::ResourceNotFound,
                   http::status::not_found, request);
}

string_response_t server_error(std::string const &message, error_type_e type,
                               string_request_t const &request) {
  return get_error(message, type, http::status::internal_server_error, request);
}

string_response_t bad_request(std::string const &message,
                              string_request_t const &request) {
  return get_error(message, error_type_e::BadRequest, http::status::bad_request,
                   request);
}

string_response_t method_not_allowed(string_request_t const &req) {
  return get_error("method not allowed", error_type_e::MethodNotAllowed,
                   http::status::method_not_allowed, req);
}

string_response_t json_success(json const &body, string_request_t const &req) {
  string_response_t response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = body.dump();
  response.prepare_payload();
  return response;
}

string_response_t success(char const *message, string_request_t const &req) {
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

string_response_t allowed_options(std::vector<http::verb> const &verbs,
                                  string_request_t const &request) {
  std::string buffer{};
  for (size_t i = 0; i < verbs.size() - 1; ++i)
    buffer += std::string(http::to_string(verbs[i])) + ", ";
  buffer += std::string(http::to_string(verbs.back()));

  using http::field;

  string_response_t response{http::status::ok, request.version()};
  response.set(field::allow, buffer);
  response.set(field::cache_control, "max-age=604800");
  response.set(field::server, "kmj-server");
  response.set(field::access_control_allow_origin, "*");
  response.set(field::access_control_allow_methods, "GET, POST");
  response.set(http::field::accept_language, "en-us,en;q=0.5");
  response.set(field::access_control_allow_headers,
               "Content-Type, Authorization");
  response.keep_alive(request.keep_alive());
  response.body() = {};
  response.prepare_payload();
  return response;
}

url_query_t split_optional_queries(boost::string_view const &optional_query) {
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
} // namespace details

using namespace details;

std::optional<account_monitor_task_result_t>
queue_account_stream_tasks(account_scheduled_task_t const &task);

enum constant_e { RequestBodySize = 1'024 * 1'024 * 50 };

std::uint64_t milliseconds_from_string(duration_unit_e const duration,
                                       json::number_integer_t const t) {
  switch (duration) {
  case duration_unit_e::minutes:
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::minutes(t))
        .count();
  case duration_unit_e::seconds:
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::seconds(t))
        .count();
  case duration_unit_e::hours:
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::hours(t))
        .count();
  case duration_unit_e::days:
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::hours(t * 24))
        .count();
  case duration_unit_e::weeks:
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::hours(t * 24 * 7))
        .count();
  case duration_unit_e::invalid:
    break;
  }
  return 0;
}

session_t::session_t(net::io_context &io, net::ip::tcp::socket &&socket)
    : m_ioContext{io}, m_tcpStream{std::move(socket)} {}

bool session_t::is_json_request() const {
  return boost::iequals(m_contentType, "application/json");
}

std::shared_ptr<session_t> session_t::add_endpoint_interfaces() {
  using http::verb;

  m_endpoints.add_endpoint("/add_account_monitoring",
                           JSON_ROUTE_CALLBACK(monitor_user_account),
                           verb::post);
  m_endpoints.add_endpoint("/add_pricing_tasks",
                           JSON_ROUTE_CALLBACK(add_new_pricing_tasks),
                           verb::post);
  m_endpoints.add_endpoint("/stop_price_tasks",
                           JSON_ROUTE_CALLBACK(stop_prices_task), verb::post);
  m_endpoints.add_endpoint("/all_price_tasks",
                           ROUTE_CALLBACK(get_all_running_price_tasks),
                           verb::get);
  m_endpoints.add_special_endpoint("/new_telegram_message/{chat_id}",
                                   JSON_ROUTE_CALLBACK(send_telegram_text),
                                   verb::post);
  m_endpoints.add_special_endpoint(
      "/new_telegram_registration_code/{number}/{code}",
      JSON_ROUTE_CALLBACK(new_telegram_registration_code_callback), verb::put);
  m_endpoints.add_special_endpoint(
      "/new_telegram_registration_password/{number}/{password}",
      JSON_ROUTE_CALLBACK(new_telegram_registration_password_callback),
      verb::put);
  m_endpoints.add_special_endpoint("/list_price_tasks/{user_id}",
                                   ROUTE_CALLBACK(get_prices_task_status),
                                   verb::get);
  m_endpoints.add_special_endpoint("/latest_price/{exchange}/{trade}/{symbol}",
                                   ROUTE_CALLBACK(latest_price_handler),
                                   verb::get);
  m_endpoints.add_special_endpoint("/trading_pairs/{exchange}",
                                   ROUTE_CALLBACK(get_trading_pairs_handler),
                                   verb::get);

  return shared_from_this();
}

void session_t::run() { http_read_data(); }

void session_t::send_response(string_response_t &&response) {
  auto resp = std::make_shared<string_response_t>(std::move(response));
  m_cachedResponse = resp;
  http::async_write(m_tcpStream, *resp,
                    beast::bind_front_handler(&session_t::on_data_written,
                                              shared_from_this()));
}

void session_t::http_read_data() {
  m_buffer.clear();
  m_clientRequest.emplace();
  m_clientRequest->body_limit(RequestBodySize);
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::minutes(5));
  http::async_read(m_tcpStream, m_buffer, *m_clientRequest,
                   ASYNC_CALLBACK(on_data_read));
}

void session_t::handle_requests(string_request_t const &request) {
  std::string request_target{utils::decodeUrl(request.target())};
  while (boost::ends_with(request_target, "/"))
    request_target.pop_back();
  m_thisRequest = request;

  if (request_target.empty())
    return error_handler(details::not_found(request));

  auto const method = request.method();
  boost::string_view request_target_view = request_target;
  auto split = utils::splitStringView(request_target_view, "?");
  auto const &target = split[0];

  // check the usual rules "table", otherwise check the special routes
  if (auto iter = m_endpoints.get_rules(target); iter.has_value()) {
    if (method == http::verb::options) {
      return send_response(
          allowed_options(iter.value()->second.verbs, request));
    }

    auto const iter_end = iter.value()->second.verbs.cend();
    auto const found_iter =
        std::find(iter.value()->second.verbs.cbegin(), iter_end, method);
    if (found_iter == iter_end)
      return error_handler(method_not_allowed(request));
    boost::string_view const query_string = split.size() > 1 ? split[1] : "";
    auto url_query{split_optional_queries(query_string)};
    return iter.value()->second.route_callback(url_query);
  }

  auto res = m_endpoints.get_special_rules(target);
  if (!res.has_value())
    return error_handler(not_found(request));

  auto &placeholder = *res;
  if (method == http::verb::options)
    return send_response(allowed_options(placeholder.rule->verbs, request));
  auto &rule = placeholder.rule.value();
  auto const found_iter =
      std::find(rule.verbs.cbegin(), rule.verbs.cend(), method);
  if (found_iter == rule.verbs.end())
    return error_handler(method_not_allowed(request));

  boost::string_view const query_string = split.size() > 1 ? split[1] : "";
  auto url_query{split_optional_queries(query_string)};

  for (auto const &[key, value] : placeholder.placeholders)
    url_query[key] = value;
  rule.route_callback(url_query);
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
                                      string_request_t{}),
                         true);
  }
  auto const content_type = m_clientRequest->get()[http::field::content_type];
  // For some reason, ^^ content_type is implicitly convertible to std::string
  // on my machine but not inside the docker container.
  m_contentType = std::string(content_type.data(), content_type.size());
  return handle_requests(m_clientRequest->get());
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
  m_cachedResponse = resp;
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

void session_t::on_data_written(
    beast::error_code ec, [[maybe_unused]] std::size_t const bytes_written) {
  if (ec)
    return spdlog::error(ec.message());

  m_cachedResponse = nullptr;
  http_read_data();
}

void session_t::get_trading_pairs_handler(url_query_t const &optional_query) {
  auto const &request = m_thisRequest;
  auto const exchange_iter = optional_query.find("exchange");
  if (exchange_iter == optional_query.end())
    return error_handler(bad_request("query `exchange` missing", request));

  auto const exchange =
      utils::stringToExchange(boost::to_lower_copy(exchange_iter->second));

  if (exchange_e::total == exchange)
    return error_handler(bad_request("invalid exchange specified", request));

  auto const names = uniqueInstruments[exchange].to_list();
  return send_response(json_success(names, request));
}

void session_t::get_prices_task_status(url_query_t const &optional_query) {
  auto const &request = m_thisRequest;

  auto const user_id_iter = optional_query.find("user_id");
  if (user_id_iter == optional_query.end() || user_id_iter->second.empty())
    return error_handler(bad_request("query `user_id` missing", request));
  auto const task_list = get_price_tasks_for_user(user_id_iter->second);
  return send_response(json_success(task_list, request));
}

void session_t::latest_price_handler(url_query_t const &optional_query) {
  auto const symbol_iter = optional_query.find("symbol");
  auto const exchange_iter = optional_query.find("exchange");
  auto const trade_iter = optional_query.find("trade");
  if (utils::anyElementIsInvalid(optional_query, symbol_iter, exchange_iter,
                                 trade_iter)) {
    return error_handler(
        bad_request("query symbol/exchange/trade missing", m_thisRequest));
  }

  auto const exchange =
      utils::stringToExchange(utils::toLowerCopy(exchange_iter->second));
  instrument_type_t instr;
  instr.name = utils::trimCopy(utils::toUpperCopy(symbol_iter->second));
  instr.tradeType =
      utils::stringToTradeType(utils::toLowerCopy(trade_iter->second));

  if (instr.name.empty() || exchange == exchange_e::total ||
      instr.tradeType == trade_type_e::total) {
    return error_handler(bad_request("malformed query", m_thisRequest));
  }

  auto &tokens = uniqueInstruments[exchange];
  auto result = tokens.find_item(instr);
  if (result.has_value())
    return send_response(json_success(*result, m_thisRequest));
  return send_response(json_success("not found", m_thisRequest));
}

void session_t::get_all_running_price_tasks(url_query_t const &) {
  send_response(json_success(get_price_tasks_for_all(), m_thisRequest));
}

void session_t::stop_prices_task(url_query_t const &) {
  try {
    auto const jsonRoot =
        json::parse(m_thisRequest.body()).get<json::object_t>();
    auto const taskListIter = jsonRoot.find("task_list");
    auto const userIDIter = jsonRoot.find("user_id");
    if (utils::anyElementIsInvalid(jsonRoot, taskListIter, userIDIter))
      throw std::runtime_error("key data needed to schedule task is missing");

    auto const userID = userIDIter->second.get<json::string_t>();
    auto const taskList = taskListIter->second.get<json::array_t>();
    scheduled_price_task_t task{};
    for (auto const &temp : taskList) {
      task.task_id = temp.get<json::string_t>();
      task.user_id = userID;
      stop_scheduled_price_task(task);
    }
    return send_response(json_success(taskList, m_thisRequest));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", m_thisRequest));
  }
}

void session_t::monitor_user_account(url_query_t const &) {
  try {
    auto const jsonRoot =
        json::parse(m_thisRequest.body()).get<json::object_t>();
    auto const exchangeIter = jsonRoot.find("exchange");
    auto const secretKeyIter = jsonRoot.find("secret_key");
    auto const passphraseIter = jsonRoot.find("pass_phrase");
    auto const apiKeyIter = jsonRoot.find("api_key");
    auto const tradeTypeIter = jsonRoot.find("trade_type");
    auto const taskIDIter = jsonRoot.find("task_id");
    auto const userIDIter = jsonRoot.find("user_id");
    if (utils::anyElementIsInvalid(jsonRoot, tradeTypeIter, exchangeIter,
                                   secretKeyIter, passphraseIter, apiKeyIter,
                                   taskIDIter, userIDIter)) {
      throw std::runtime_error("key data needed to schedule task is missing");
    }

    account_scheduled_task_t task{};
    task.taskID = taskIDIter->second.get<json::string_t>();
    task.userID = userIDIter->second.get<json::string_t>();
    task.exchange =
        utils::stringToExchange(exchangeIter->second.get<json::string_t>());
    task.tradeType =
        utils::stringToTradeType(tradeTypeIter->second.get<json::string_t>());

    if (task.exchange == exchange_e::total) {
      return error_handler(
          bad_request("Invalid exchange specified", m_thisRequest));
    }
    // trade type is only necessary for a kucoin account
    if (task.exchange == exchange_e::kucoin &&
        task.tradeType == trade_type_e::total) {
      return error_handler(bad_request(
          "Invalid trade type specified for kucoin data", m_thisRequest));
    }

    task.passphrase = passphraseIter->second.get<json::string_t>();
    task.secretKey = secretKeyIter->second.get<json::string_t>();
    task.apiKey = apiKeyIter->second.get<json::string_t>();
    task.operation = task_operation_e::add;

    if (task.taskID.empty()) {
      auto const errorMessage = "the task ID supplied is empty, cannot proceed";
      return error_handler(bad_request(errorMessage, m_thisRequest));
    }

    spdlog::info("Account monitoring scheduled...{} {}", task.userID,
                 task.taskID);

    auto const optResult = queue_account_stream_tasks(task);
    if (!optResult.has_value()) {
      return error_handler(
          server_error("there was a problem scheduling this task",
                       error_type_e::ServerError, m_thisRequest));
    }

    json::object_t jsonObject;
    jsonObject["task_id"] = task.taskID;
    jsonObject["state"] = (int)optResult->state;

    return send_response(json_success(jsonObject, m_thisRequest));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", m_thisRequest));
  }
}

void session_t::new_telegram_registration_code_callback(
    url_query_t const &query) {
  auto mobile_number_iter = query.find("number");
  auto code_iter = query.find("code");
  if (utils::anyElementIsInvalid(query, mobile_number_iter, code_iter)) {
    return error_handler(
        bad_request("mobile number or code missing", m_thisRequest));
  }
  auto const mobile_number = mobile_number_iter->second;
  auto const code = code_iter->second;
  send_telegram_registration_code(mobile_number, code);
  return send_response(json_success("OK", m_thisRequest));
}

void session_t::new_telegram_registration_password_callback(
    url_query_t const &query) {
  auto mobile_number_iter = query.find("number");
  auto password_iter = query.find("password");
  if (utils::anyElementIsInvalid(query, mobile_number_iter, password_iter)) {
    return error_handler(
        bad_request("mobile number or password missing", m_thisRequest));
  }
  auto const mobile_number = mobile_number_iter->second;
  auto const password = password_iter->second;
  send_telegram_registration_password(mobile_number, password);
  return send_response(json_success("OK", m_thisRequest));
}

void session_t::send_telegram_text(const keep_my_journal::url_query_t &query) {
  auto chat_id_iter = query.find("chat_id");
  if (chat_id_iter == query.end())
    return error_handler(bad_request("chat id is missing", m_thisRequest));

  std::string content{};
  int64_t chat_id = 0;

  try {
    auto const json_root =
        json::parse(m_thisRequest.body()).get<json::object_t>();
    auto const content_iter = json_root.find("content");
    if (content_iter == json_root.end()) {
      return error_handler(
          bad_request("chat content is missing", m_thisRequest));
    }
    content = content_iter->second.get<json::string_t>();
    chat_id = std::stoll(chat_id_iter->second);
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(
        bad_request("badly formed JSON content", m_thisRequest));
  }

  if (content.empty() || chat_id == 0) {
    return error_handler(
        bad_request("content or chat id missing", m_thisRequest));
  }
  send_new_telegram_text(chat_id, content);
  return send_response(json_success("OK", m_thisRequest));
}

void session_t::add_new_pricing_tasks(url_query_t const &) {
  try {
    auto const json_root =
        json::parse(m_thisRequest.body()).get<json::object_t>();
    auto const request_id_iter = json_root.find("task_id");
    auto const user_id_iter = json_root.find("user_id");
    if (utils::anyElementIsInvalid(json_root, request_id_iter, user_id_iter)) {
      return error_handler(
          bad_request("request/user ID missing", m_thisRequest));
    }
    auto const request_id = request_id_iter->second.get<json::string_t>();
    auto const user_id = user_id_iter->second.get<json::string_t>();

    auto const json_job_list = json_root.at("contracts").get<json::array_t>();
    size_t const job_size = json_job_list.size();

    std::vector<scheduled_price_task_t> erredTasks;
    erredTasks.reserve(job_size);

    spdlog::info("JobSize: {}", job_size);

    for (size_t i = 0; i < job_size; ++i) {
      auto const &json_object = json_job_list[i].get<json::object_t>();
      scheduled_price_task_t new_task{};

      auto const symbols = json_object.at("symbols").get<json::array_t>();
      for (auto const &symbol : symbols) {
        new_task.tokens.push_back(
            boost::to_upper_copy(symbol.get<json::string_t>()));
      }

      new_task.user_id = user_id;
      new_task.task_id = request_id;
      new_task.tradeType = utils::stringToTradeType(
          json_object.at("trade").get<json::string_t>());
      new_task.exchange = utils::stringToExchange(
          json_object.at("exchange").get<json::string_t>());

      auto intervalIter = json_object.find("intervals");
      auto percentageIter = json_object.find("percentage");

      if (intervalIter != json_object.end()) {
        new_task.timeProp
            .emplace<scheduled_price_task_t::timed_based_property_t>({});

        auto durationIter = json_object.find("duration");
        if (durationIter == json_object.end()) {
          return error_handler(
              bad_request("duration not specified", m_thisRequest));
        }
        json::number_integer_t interval = 0;
        if (intervalIter->second.is_number())
          interval = intervalIter->second.get<json::number_integer_t>();
        else
          interval = std::stoi(intervalIter->second.get<json::string_t>());
        auto const duration = durationIter->second.get<json::string_t>();
        new_task.timeProp->duration = utils::stringToDurationUnit(duration);

        auto const intervalDur =
            milliseconds_from_string(new_task.timeProp->duration, interval);
        if (intervalDur == 0) {
          return error_handler(bad_request(
              "something is wrong with the duration", m_thisRequest));
        }
        new_task.timeProp->timeMS = intervalDur;
      } else if (percentageIter != json_object.end()) {
        new_task.percentProp
            .emplace<scheduled_price_task_t::percentage_based_property_t>({});

        auto directionIter = json_object.find("direction");
        if (directionIter == json_object.end()) {
          return error_handler(
              bad_request("direction not specified", m_thisRequest));
        }
        auto percentage =
            std::abs(percentageIter->second.get<json::number_float_t>());
        auto const direction =
            boost::to_lower_copy(directionIter->second.get<json::string_t>());
        new_task.percentProp->direction =
            utils::stringToPriceDirection(direction);

        if (new_task.percentProp->direction == price_direction_e::invalid) {
          return error_handler(
              bad_request("something is wrong with the specified direction",
                          m_thisRequest));
        }

        if (new_task.percentProp->direction == price_direction_e::down)
          percentage *= -1.0;

        if (percentage == 0.0) {
          return error_handler(
              bad_request("invalid percentage specified", m_thisRequest));
        }
        new_task.percentProp->percentage = percentage;
      }

      new_task.status = task_state_e::initiated;
      if (!schedule_new_price_task(new_task))
        erredTasks.push_back(new_task);
    }

    json::object_t result;
    result["status"] = static_cast<int>(error_type_e::NoError);
    result["message"] = "OK";
    result["failed"] = erredTasks;

    if (!request_id.empty())
      result["id"] = request_id;

    return send_response(json_success(result, m_thisRequest));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", m_thisRequest));
  }
}
} // namespace keep_my_journal
