#pragma once

#include "commodity.hpp"
#include <optional>
#include <vector>

namespace keep_my_journal {
struct scheduled_price_task_t {
  struct timed_based_property_t {
    uint64_t timeMS{};
    duration_unit_e duration = duration_unit_e::invalid;
#ifdef CRYPTOLOG_USING_MSGPACK
    MSGPACK_DEFINE(timeMS, duration);
#endif
  };

  struct percentage_based_property_t {
    double percentage{};
    price_direction_e direction = price_direction_e::invalid;
#ifdef CRYPTOLOG_USING_MSGPACK
    MSGPACK_DEFINE(percentage, direction);
#endif
  };

  std::string task_id;
  std::string user_id;

  std::vector<std::string> tokens;
  trade_type_e tradeType = trade_type_e::total;
  exchange_e exchange = exchange_e::total;
  std::optional<percentage_based_property_t> percentProp = std::nullopt;
  std::optional<timed_based_property_t> timeProp = std::nullopt;
  task_state_e status;

#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(task_id, user_id, tokens, tradeType, exchange, percentProp,
                 timeProp, status);
#endif
};

struct scheduled_price_task_result_t {
  scheduled_price_task_t task;
  std::vector<instrument_type_t> result;

#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(task, result);
#endif
};

} // namespace keep_my_journal