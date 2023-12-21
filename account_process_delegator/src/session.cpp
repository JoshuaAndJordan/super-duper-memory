// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include "session.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>

#include <account_stream/user_scheduled_task.hpp>
#include <jwt/jwt.hpp>
#include <price_stream/commodity.hpp>

#include "crypto_utils.hpp"
#include "enumerations.hpp"
#include "json_utils.hpp"
#include "scheduled_price_tasks.hpp"
#include "string_utils.hpp"

namespace keep_my_journal {

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

std::optional<endpoint_t::rule_iterator>
endpoint_t::get_rules(boost::string_view const &target) {
  return get_rules(target.to_string());
}

void to_json(json &j, instrument_type_t const &instr) {
  j = json{{"name", instr.name},
           {"price", instr.currentPrice},
           {"open24hr", instr.open24h},
           {"type", utils::tradeTypeToString(instr.tradeType)}};
}

void to_json(json &j, scheduled_price_task_t const &data) {
  json::object_t obj;
  obj["task_id"] = data.task_id;
  obj["exchange"] = utils::exchangesToString(data.exchange);
  obj["trade_type"] = utils::tradeTypeToString(data.tradeType);
  obj["symbols"] = data.tokens;

  if (data.timeProp) {
    obj["intervals"] = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::milliseconds(data.timeProp->timeMS))
                           .count();
    obj["duration"] = "seconds";
  } else if (data.percentProp) {
    obj["direction"] = data.percentProp->percentage < 0 ? "down" : "up";
    obj["percentage"] = std::abs(data.percentProp->percentage);
  }

  j = obj;
}

session_t::session_t(net::io_context &io, net::ip::tcp::socket &&socket)
    : m_ioContext{io}, m_tcpStream{std::move(socket)} {}

std::shared_ptr<session_t> session_t::add_endpoint_interfaces() {
  using http::verb;

  m_endpointApis.add_endpoint("/add_pricing_task", {verb::post},
                              JSON_ROUTE_CALLBACK(add_new_pricing_tasks));
  m_endpointApis.add_endpoint("/trading_pairs", {verb::get},
                              JSON_ROUTE_CALLBACK(get_trading_pairs_handler));
  m_endpointApis.add_endpoint("/account_monitoring_task", {verb::post},
                              JSON_ROUTE_CALLBACK(monitor_user_account));
  m_endpointApis.add_endpoint("/price_task_status", {verb::get},
                              JSON_ROUTE_CALLBACK(get_prices_task_status));
  m_endpointApis.add_endpoint("/stop_price_task", {verb::post},
                              JSON_ROUTE_CALLBACK(stop_prices_task));
  return shared_from_this();
}

void session_t::http_write(beast::tcp_stream &tcpStream,
                           file_serializer_t &fileSerializer,
                           std::function<void()> func) {
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
    return error_handler(not_found(request));

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
                                      string_request_t{}),
                         true);
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

void session_t::on_data_written(
    beast::error_code ec, [[maybe_unused]] std::size_t const bytes_written) {
  if (ec)
    return spdlog::error(ec.message());

  m_resp = nullptr;
  http_read_data();
}

void session_t::get_trading_pairs_handler(string_request_t const &request,
                                          url_query_t const &optional_query) {
  auto const exchange_iter = optional_query.find("exchange");
  if (exchange_iter == optional_query.end())
    return error_handler(bad_request("query `exchange` missing", request));

  auto const exchange = utils::stringToExchange(
      boost::to_lower_copy(exchange_iter->second.to_string()));

  if (exchange_e::total == exchange)
    return error_handler(bad_request("invalid exchange specified", request));

  auto const names =
      instrument_sink_t::get_unique_instruments(exchange).to_list();
  return send_response(json_success(names, request));
}

void session_t::get_prices_task_status(string_request_t const &request,
                                       url_query_t const &optional_query) {
  auto const user_id_iter = optional_query.find("user_id");
  if (user_id_iter == optional_query.end() || user_id_iter->second.empty())
    return error_handler(bad_request("query `user_id` missing", request));
  auto const task_list =
      get_price_tasks_for_user(user_id_iter->second.to_string());
  return send_response(json_success(task_list, request));
}

void session_t::stop_prices_task(string_request_t const &request,
                                 url_query_t const &) {
  try {
    auto const jsonRoot = json::parse(request.body()).get<json::object_t>();
    auto const taskListIter = jsonRoot.find("task_list");
    auto const userIDIter = jsonRoot.find("user_id");
    if (utils::anyElementIsInvalid(jsonRoot, taskListIter, userIDIter)) {
      throw std::runtime_error("key data needed to schedule task is missing");
    }

    auto const userID = userIDIter->second.get<json::string_t>();
    auto const taskList = taskListIter->second.get<json::array_t>();
    scheduled_price_task_t task;
    for (auto const &temp : taskList) {
      task.task_id = temp.get<json::string_t>();
      task.user_id = userID;
      stop_scheduled_price_task(task);
    }

    return send_response(json_success(json::array_t{}, request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", request));
  }
}

void session_t::monitor_user_account(string_request_t const &request,
                                     url_query_t const &) {
  try {
    auto const jsonRoot = json::parse(request.body()).get<json::object_t>();
    auto const exchangeIter = jsonRoot.find("exchange");
    auto const secretKeyIter = jsonRoot.find("secret_key");
    auto const passphraseIter = jsonRoot.find("passphrase");
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

    if (task.exchange == exchange_e::total)
      return error_handler(bad_request("Invalid exchange specified", request));

    // trade type is only necessary for a kucoin account
    if (task.exchange == exchange_e::kucoin &&
        task.tradeType == trade_type_e::total) {
      return error_handler(
          bad_request("Invalid trade type specified for kucoin data", request));
    }

    task.passphrase = passphraseIter->second.get<json::string_t>();
    task.secretKey = secretKeyIter->second.get<json::string_t>();
    task.apiKey = apiKeyIter->second.get<json::string_t>();
    task.operation = task_operation_e::add;

    if (task.taskID.empty()) {
      auto const errorMessage = "the task ID supplied is empty, cannot proceed";
      return error_handler(bad_request(errorMessage, request));
    }

    auto const optResult = queue_account_stream_tasks(task);
    if (!optResult.has_value()) {
      return error_handler(
          server_error("there was a problem scheduling this task",
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

void session_t::add_new_pricing_tasks(string_request_t const &request,
                                      url_query_t const &) {
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    std::string request_id{};
    if (auto const request_id_iter = json_root.find("id");
        json_root.end() != request_id_iter) {
      request_id = request_id_iter->second.get<json::string_t>();
    }

    auto const json_job_list = json_root.at("contracts").get<json::array_t>();
    size_t const job_size = json_job_list.size();

    std::vector<scheduled_price_task_t> erredTasks;
    erredTasks.reserve(job_size);

    for (size_t i = 0; i < job_size; ++i) {
      auto const &json_object = json_job_list[i].get<json::object_t>();
      scheduled_price_task_t new_task{};

      auto const symbols = json_object.at("symbols").get<json::array_t>();
      for (auto const &symbol : symbols) {
        new_task.tokens.push_back(
            boost::to_upper_copy(symbol.get<json::string_t>()));
      }

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
        if (durationIter == json_object.end())
          return error_handler(bad_request("duration not specified", request));
        auto const interval =
            intervalIter->second.get<json::number_integer_t>();
        auto const duration = durationIter->second.get<json::string_t>();
        new_task.timeProp->duration = utils::stringToDurationUnit(duration);

        auto const intervalDur =
            milliseconds_from_string(new_task.timeProp->duration, interval);
        if (intervalDur == 0) {
          return error_handler(
              bad_request("something is wrong with the duration", request));
        }
        new_task.timeProp->timeMS = intervalDur;
      } else if (percentageIter != json_object.end()) {
        new_task.percentProp
            .emplace<scheduled_price_task_t::percentage_based_property_t>({});

        auto directionIter = json_object.find("direction");
        if (directionIter == json_object.end())
          return error_handler(bad_request("direction not specified", request));

        auto percentage =
            std::abs(percentageIter->second.get<json::number_float_t>());
        auto const direction =
            boost::to_lower_copy(directionIter->second.get<json::string_t>());
        new_task.percentProp->direction =
            utils::stringToPriceDirection(direction);

        if (new_task.percentProp->direction == price_direction_e::invalid)
          return error_handler(bad_request(
              "something is wrong with the specified direction", request));

        if (new_task.percentProp->direction == price_direction_e::down)
          percentage *= -1.0;

        if (percentage == 0.0)
          return error_handler(
              bad_request("invalid percentage specified", request));

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

    return send_response(json_success(result, request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("JSON object is invalid", request));
  }
}

bool session_t::is_json_request() const {
  return boost::iequals(m_contentType, "application/json");
}

// =========================STATIC FUNCTIONS==============================

string_response_t session_t::not_found(string_request_t const &request) {
  return get_error("url not found", error_type_e::ResourceNotFound,
                   http::status::not_found, request);
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

} // namespace keep_my_journal
