#pragma once

#include "enumerations.hpp"
#include <string>
#include <variant>

namespace jordan {
struct scheduled_price_task_t {
  struct price_task_sm_t {
    std::string socialMediaName;
    std::string username;
  };

  std::string forUserID;
  std::vector<std::string> symbols;
  std::vector<trade_type_e> tradeTypes{};
  std::size_t intervalInSeconds{};
  std::vector<price_task_sm_t> socials;
};

struct task_result_t {
  /* this struct is created periodically, if there was a way to avoid copying
   * strings every time, we should take it. */
  std::string requestID{};
  std::string symbol{};
  std::string forUsername{};
  std::string currentTime{};
  trade_direction_e direction{trade_direction_e::none};
  task_type_e taskType{task_type_e::unknown};
  std::size_t columnID{};
  double orderPrice{};
  double marketPrice{};
  double money{};
  double quantity{};
  double pnl{};
};

struct user_task_t {
  std::string requestID{};
  std::string tokenName{};
  std::string direction{};
  std::string createdTime{};
  std::string lastBeginTime{}; // last time the task started
  std::string lastEndTime{};   // last time the task ended
  std::size_t columnID{};
  std::size_t monitorTimeSecs{};
  task_state_e status{task_state_e::unknown};
  task_type_e taskType{task_type_e::unknown};
  double money{};
  double orderPrice{};
  double quantity{};
};

using scheduled_task_result_t = std::variant<scheduled_task_t, task_result_t>;
std::string task_state_to_string(task_state_e const);
std::string direction_to_string(trade_direction_e const);
trade_direction_e string_to_direction(std::string const &);

} // namespace jordan
