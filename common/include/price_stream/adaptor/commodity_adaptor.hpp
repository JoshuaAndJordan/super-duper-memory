#pragma once

#include "scheduled_task_adaptor.hpp"

namespace keep_my_journal::dbus::adaptor {
using dbus_instrument_type_t =
    typename sdbus::Struct<std::string, double, double, int>;
using dbus_time_task_result_t =
    typename sdbus::Struct<dbus_time_task_t,
                           std::vector<dbus_instrument_type_t>>;
using dbus_progress_task_result_t =
    typename sdbus::Struct<dbus_progress_struct_t,
                           std::vector<dbus_instrument_type_t>>;
} // namespace keep_my_journal::dbus::adaptor