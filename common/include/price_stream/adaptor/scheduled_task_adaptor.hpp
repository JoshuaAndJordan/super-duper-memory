#pragma once

#include "price_stream/tasks.hpp"
#include <sdbus-c++/Message.h>
#include <sdbus-c++/Types.h>

namespace keep_my_journal::dbus::adaptor {
struct progress_task_t {
  uint64_t process_assigned_id = 0;
  double percentage = 0.0;
  int32_t direction_enum = static_cast<int32_t>(price_direction_e::invalid);
  int32_t trade_type_enum = static_cast<int32_t>(trade_type_e::total);
  int32_t exchange_enum = static_cast<int32_t>(exchange_e::total);
  int32_t task_status_enum = static_cast<int32_t>(task_state_e::unknown);
  std::string task_id{};
  std::string user_id{};
  std::vector<std::string> tokens{};
};

struct time_based_task_t {
  uint64_t process_assigned_id = 0;
  uint64_t time = 0;
  int32_t duration_enum = static_cast<int32_t>(duration_unit_e::invalid);
  int32_t trade_type_enum = static_cast<int32_t>(trade_type_e::total);
  int32_t exchange_enum = static_cast<int32_t>(exchange_e::total);
  int32_t task_status_enum = static_cast<int32_t>(task_state_e::unknown);
  std::string task_id{};
  std::string user_id{};
  std::vector<std::string> tokens{};
};

using dbus_time_task_t =
    typename sdbus::Struct<uint64_t, uint64_t, int32_t, int32_t, int32_t,
                           int32_t, std::string, std::string,
                           std::vector<std::string>>;

using dbus_progress_struct_t =
    typename sdbus::Struct<uint64_t, double, int32_t, int32_t, int32_t, int32_t,
                           std::string, std::string, std::vector<std::string>>;

[[nodiscard]] dbus_time_task_t
scheduled_task_to_dbus_time(scheduled_price_task_t const &);
[[nodiscard]] scheduled_price_task_t
dbus_time_to_scheduled_task(dbus_time_task_t const &);
[[nodiscard]] scheduled_price_task_t
dbus_progress_to_scheduled_task(dbus_progress_struct_t const &);
[[nodiscard]] dbus_progress_struct_t
scheduled_task_to_dbus_progress(scheduled_price_task_t const &);
} // namespace keep_my_journal::dbus::adaptor

namespace sdbus {
namespace kmj_adaptor = keep_my_journal::dbus::adaptor;

template <>
struct signature_of<kmj_adaptor::progress_task_t>
    : signature_of<kmj_adaptor::dbus_progress_struct_t> {};

template <>
struct signature_of<kmj_adaptor::time_based_task_t>
    : signature_of<kmj_adaptor::dbus_time_task_t> {};

Message &operator<<(Message &msg, kmj_adaptor::progress_task_t const &task);
Message &operator>>(Message &message, kmj_adaptor::progress_task_t &task);
Message &operator<<(Message &msg, kmj_adaptor::time_based_task_t const &task);
Message &operator>>(Message &message, kmj_adaptor::time_based_task_t &task);

} // namespace sdbus
