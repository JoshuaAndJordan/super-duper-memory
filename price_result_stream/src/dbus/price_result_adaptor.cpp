#include "dbus/price_result_adaptor.hpp"
#include <spdlog/spdlog.h>

namespace keep_my_journal {
void price_result_stream_t::broadcast_progress_price_result(
    dbus_progress_task_result_t const &res) {
  auto const &task =
      dbus::adaptor::dbus_progress_to_scheduled_task(res.get<0>());
  std::vector<dbus::adaptor::dbus_instrument_type_t> const &tokens =
      res.get<1>();
  spdlog::info("{}: ID: {}, Size: {}", __func__, task.process_assigned_id,
               tokens.size());
}

void price_result_stream_t::broadcast_time_price_result(
    keep_my_journal::dbus_time_task_result_t const &res) {
  auto const &task = dbus::adaptor::dbus_time_to_scheduled_task(res.get<0>());
  std::vector<dbus::adaptor::dbus_instrument_type_t> const &tokens =
      res.get<1>();
  spdlog::info("{}: ID: {}, Size: {}", __func__, task.process_assigned_id,
               tokens.size());
}
} // namespace keep_my_journal