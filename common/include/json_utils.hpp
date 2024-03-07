// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include "account_stream/binance_order_info.hpp"
#include "account_stream/okex_order_info.hpp"
#include "price_stream/tasks.hpp"
#include <nlohmann/json.hpp>
#include <optional>

using nlohmann::json;

// the freestanding `to_json` functions are used by the JSON library and need
// to be in the same namespace as the classes

namespace keep_my_journal {
namespace utils {
template <typename T>
T get_json_value(json::object_t const &data, std::string const &key) {
  if constexpr (std::is_same_v<T, json::number_integer_t>) {
    return data.at(key).get<json::number_integer_t>();
  } else if constexpr (std::is_same_v<T, json::string_t>) {
    return data.at(key).get<json::string_t>();
  } else if constexpr (std::is_same_v<T, json::number_float_t>) {
    return data.at(key).get<json::number_float_t>();
  }
  return {};
}

template <typename T>
void get_object_member(json::object_t const &json_object,
                       std::string const &member, T &result) {
  auto iter = json_object.find(member);
  if (iter == json_object.end())
    throw std::runtime_error(member + " does not exist");
  result = iter->second.get<T>();
}

std::optional<json::object_t> read_object_json_file(std::string const &);
} // namespace utils

void to_json(json &j, scheduled_price_task_t const &data);
void to_json(json &j, instrument_type_t const &instr);
namespace binance {
void to_json(json &j, ws_account_update_t const &);
void to_json(json &j, ws_order_info_t const &);
void to_json(json &j, ws_balance_info_t const &);
} // namespace binance
namespace okex {
void to_json(json &j, ws_order_info_t const &);
void to_json(json &j, ws_balance_data_t const &);
} // namespace okex
} // namespace keep_my_journal
