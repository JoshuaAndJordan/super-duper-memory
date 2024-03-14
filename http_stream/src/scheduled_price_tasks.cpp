// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include "scheduled_price_tasks.hpp"

#include "dbus/use_cases/progress_proxy_client_impl.hpp"
#include "dbus/use_cases/telegram_proxy_client_impl.hpp"
#include "dbus/use_cases/time_proxy_client_impl.hpp"
#include "price_stream/adaptor/scheduled_task_adaptor.hpp"
#include <spdlog/spdlog.h>

namespace keep_my_journal {
char const *const time_dbus_destination_path = "keep.my.journal.time";
char const *const time_dbus_object_path = "/keep/my/journal/time/1";
char const *const progress_dbus_destination_path = "keep.my.journal.progress";
char const *const progress_dbus_object_path = "/keep/my/journal/progress/1";
char const *const telegram_dbus_dest_path = "keep.my.journal.messaging.tg";
char const *telegram_dbus_object_path = "/keep/my/journal/messaging/telegram/1";

time_proxy_impl &time_dbus_client() {
  static time_proxy_impl proxyImpl(time_dbus_destination_path,
                                   time_dbus_object_path);
  return proxyImpl;
}

progress_proxy_impl &progress_dbus_client() {
  static progress_proxy_impl proxy(progress_dbus_destination_path,
                                   progress_dbus_object_path);
  return proxy;
}

telegram_proxy_impl &telegram_dbus_client() {
  static telegram_proxy_impl proxy(telegram_dbus_dest_path,
                                   telegram_dbus_object_path);
  return proxy;
}

bool passed_valid_task_check(scheduled_price_task_t &task) {
  if (task.tokens.empty() || !(task.percentProp || task.timeProp))
    return false;

  if (task.percentProp) {
    double const percentage =
        std::clamp(task.percentProp->percentage, -100.0, 100.0);
    if (percentage == 0.0)
      return false;
    task.percentProp->percentage = percentage;
  }

  if (task.timeProp && task.timeProp->timeMS <= 0)
    return false;

  if (task.exchange == exchange_e::total ||
      task.tradeType == trade_type_e::total)
    return false;

  std::sort(task.tokens.begin(), task.tokens.end());
  if (std::unique(task.tokens.begin(), task.tokens.end()) != task.tokens.end())
    return false;
  return true;
}

bool schedule_new_price_tasks(std::vector<scheduled_price_task_t> tasks) {
  if (!std::all_of(std::begin(tasks), std::end(tasks),
                   passed_valid_task_check)) {
    return false;
  }

  static uint64_t task_id = 0;
  bool result = true;
  std::vector<scheduled_price_task_t> erred_list;
  erred_list.reserve(tasks.size());

  for (auto &task : tasks) {
    task.process_assigned_id = ++task_id;
    if (task.percentProp)
      result = push_progress_based_task_to_wire(task);
    else if (task.timeProp)
      result = push_time_based_task_to_wire(task);
    if (!result)
      erred_list.push_back(task);
  }

  if (!erred_list.empty()) {
    std::for_each(erred_list.begin(), erred_list.end(),
                  stop_scheduled_price_task);
  }
  return erred_list.empty();
}

bool push_progress_based_task_to_wire(scheduled_price_task_t const &taskInfo) {
  auto &client = progress_dbus_client();
  auto const arg = dbus::adaptor::scheduled_task_to_dbus_progress(taskInfo);
  return client.schedule_new_progress_task(arg);
}

bool push_time_based_task_to_wire(scheduled_price_task_t const &taskInfo) {
  auto const arg = dbus::adaptor::scheduled_task_to_dbus_time(taskInfo);
  return time_dbus_client().schedule_new_time_task(arg);
}

void stop_scheduled_price_task(scheduled_price_task_t const &taskInfo) {
  time_dbus_client().remove_scheduled_time_task(taskInfo.user_id,
                                                taskInfo.task_id);
  progress_dbus_client().remove_scheduled_progress_task(taskInfo.user_id,
                                                        taskInfo.task_id);
}

std::vector<scheduled_price_task_t>
get_price_tasks_for_user(std::string const &userID) {
  std::vector<scheduled_price_task_t> result;

  {
    auto const res = time_dbus_client().get_scheduled_tasks_for_user(userID);
    if (!res.empty())
      result.reserve(res.size());
    for (auto const &r : res)
      result.push_back(dbus::adaptor::dbus_time_to_scheduled_task(r));
  }

  {
    auto const res =
        progress_dbus_client().get_scheduled_tasks_for_user(userID);
    result.reserve(result.size() + res.size());
    for (auto const &r : res)
      result.push_back(dbus::adaptor::dbus_progress_to_scheduled_task(r));
  }
  spdlog::info("Result returning: {}", result.size());
  return result;
}

std::vector<scheduled_price_task_t> get_price_tasks_for_all() {
  std::vector<scheduled_price_task_t> result;

  {
    auto const res = time_dbus_client().get_all_scheduled_tasks();
    if (!res.empty())
      result.reserve(res.size());
    for (auto const &r : res)
      result.push_back(dbus::adaptor::dbus_time_to_scheduled_task(r));
  }

  {
    auto const res = progress_dbus_client().get_all_scheduled_tasks();
    result.reserve(result.size() + res.size());
    for (auto const &r : res)
      result.push_back(dbus::adaptor::dbus_progress_to_scheduled_task(r));
  }
  spdlog::info("Result returning from {}: {}", __func__, result.size());

  return result;
}

void send_telegram_registration_code(std::string const &mobile,
                                     std::string const &code) {
  telegram_dbus_client().on_authorization_code_requested(mobile, code);
}

void send_telegram_registration_password(std::string const &mobile,
                                         std::string const &password) {
  telegram_dbus_client().on_authorization_password_requested(mobile, password);
}

void send_new_telegram_text(int64_t const chat_id, std::string const &content) {
  telegram_dbus_client().send_new_telegram_text(chat_id, content);
}
} // namespace keep_my_journal
