#include "json_utils.hpp"

namespace keep_my_journal {
namespace utils {
std::string exchangesToString(exchange_e);

std::string tradeTypeToString(trade_type_e);
} // namespace utils

void to_json(json &j, scheduled_price_task_result_t const &data) {
  json::object_t obj;
  obj["results"] = data.result;
  obj["task"] = data.task;
  j = obj;
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

void to_json(json &j, instrument_type_t const &instr) {
  j = json{{"name", instr.name},
           {"price", instr.currentPrice},
           {"open24hr", instr.open24h},
           {"type", utils::tradeTypeToString(instr.tradeType)}};
}

} // namespace keep_my_journal