// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include "scheduled_price_tasks.hpp"

#include "dbus/use/progress_proxy_client_impl.hpp"
#include "dbus/use/time_proxy_client_impl.hpp"
#include "price_stream/adaptor/scheduled_task_adaptor.hpp"
#include <spdlog/spdlog.h>

namespace keep_my_journal {
static char const *time_dbus_destination_path = "keep.my.journal.time";
static char const *time_dbus_object_path = "/keep/my/journal/time/1";
static char const *progress_dbus_destination_path = "keep.my.journal.progress";
static char const *progress_dbus_object_path = "/keep/my/journal/progress/1";

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

  return true;
}

bool schedule_new_price_task(scheduled_price_task_t taskInfo) {
  if (!passed_valid_task_check(taskInfo))
    return false;

  static uint64_t task_id = 0;
  taskInfo.process_assigned_id = ++task_id;

  if (taskInfo.percentProp)
    return push_progress_based_task_to_wire(std::move(taskInfo));
  else if (taskInfo.timeProp)
    return push_time_based_task_to_wire(std::move(taskInfo));
  return false;
}

bool push_progress_based_task_to_wire(scheduled_price_task_t &&taskInfo) {
  auto &client = progress_dbus_client();
  auto const arg = dbus::adaptor::scheduled_task_to_dbus_progress(taskInfo);
  return client.schedule_new_progress_task(arg);
}

bool push_time_based_task_to_wire(scheduled_price_task_t &&taskInfo) {
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

} // namespace keep_my_journal
