#pragma once

#include "dbus/base/price_task_result_server.hpp"
#include "price_stream/adaptor/commodity_adaptor.hpp"

namespace keep_my_journal {
using dbus_time_task_result_t = dbus::adaptor::dbus_time_task_result_t;
using dbus_progress_task_result_t = dbus::adaptor::dbus_progress_task_result_t;

class price_result_stream_t final
    : sdbus::AdaptorInterfaces<
          keep::my::journal::prices::interface::result_adaptor> {
public:
  price_result_stream_t(sdbus::IConnection &connection, std::string object_path)
      : sdbus::AdaptorInterfaces<
            keep::my::journal::prices::interface::result_adaptor>(
            connection, std::move(object_path)) {
    registerAdaptor();
  }
  ~price_result_stream_t() { unregisterAdaptor(); }
  void
  broadcast_progress_price_result(dbus_progress_task_result_t const &) final;
  void broadcast_time_price_result(dbus_time_task_result_t const &) final;
};
} // namespace keep_my_journal