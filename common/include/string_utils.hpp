// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <boost/utility/string_view.hpp>
#include <string_view>
#include <vector>

#ifdef CRYPTOLOG_USING_MSGPACK
#include "enumerations.hpp"
#endif

namespace keep_my_journal::utils {
template <typename Container, typename... IterList>
bool anyElementIsInvalid(Container const &container, IterList &&...iter_list) {
  return (... || (std::cend(container) == iter_list));
}

void trimString(std::string &);
std::string trimCopy(std::string const &s);
std::string toLowerCopy(std::string const &str);
std::string toUpperCopy(std::string const &str);
void toLowerString(std::string &str);
void toUpperString(std::string &str);
bool unixTimeToString(std::string &, std::size_t,
                      char const * = "%Y-%m-%d %H:%M:%S");
bool isValidMobileNumber(std::string_view, std::string &);
std::string md5Hash(std::string const &);
std::string decodeUrl(boost::string_view const &encoded_string);
std::string stringViewToString(boost::string_view const &str_view);
std::string integerListToString(std::vector<uint32_t> const &vec);
std::string_view boostViewToStdStringView(boost::string_view);
std::vector<std::string> splitStringView(boost::string_view const &str,
                                         char const *const delim);
void splitStringInto(std::vector<std::string> &, std::string const &text,
                     std::string const &delim);
void replaceIfStarts(std::string &, std::string const &findText,
                     std::string const &replaceText);

template <typename T>
inline void concatenate_data(std::ostringstream &stream, T const &data) {
  if constexpr (std::is_same_v<std::string_view, T>)
    stream << data.data();
  else
    stream << data;
}

template <typename DataType>
std::string stringListToString(std::vector<DataType> const &vec) {
  if (vec.empty())
    return {};
  std::ostringstream stream{};
  for (std::size_t index = 0; index < vec.size() - 1; ++index) {
    concatenate_data(stream, vec[index]);
    stream << ",";
  }

  concatenate_data(stream, vec.back());
  return stream.str();
}

template <typename ScheduledTask>
std::string extractTasksIDsToString(std::vector<ScheduledTask> const &tasks) {
  std::ostringstream ss;
  for (size_t index = 0; index < tasks.size() - 1; ++index)
    ss << tasks[index].task_id << ",";
  ss << tasks.back().task_id;
  return ss.str();
}

#ifdef CRYPTOLOG_USING_MSGPACK
std::string exchangesToString(exchange_e exchange);
std::string tradeTypeToString(trade_type_e tradeType);
std::string durationUnitToString(duration_unit_e);
std::string priceDirectionToString(price_direction_e);
price_direction_e stringToPriceDirection(std::string const &str);
duration_unit_e stringToDurationUnit(std::string const &str);
exchange_e stringToExchange(std::string const &exchangeName);
trade_type_e stringToTradeType(std::string const &str);
#endif
} // namespace keep_my_journal::utils
