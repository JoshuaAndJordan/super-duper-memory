#pragma once

#include <string>
#include <unordered_map>

namespace jordan {

struct pushed_subscription_data_t {
  std::string symbolID{};
  double currentPrice{};
  double open24h{};
};

using subscription_data_map_t =
    std::unordered_map<std::string, pushed_subscription_data_t>;
} // namespace jordan
