// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>

#include "container.hpp"
#include "enumerations.hpp"

namespace keep_my_journal::binance {

struct user_result_request_t {
  std::string accountAlias{};
  std::optional<std::string> startDate{};
  std::optional<std::string> endDate{};
};

} // namespace keep_my_journal::binance
