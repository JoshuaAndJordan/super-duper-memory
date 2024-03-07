#pragma once

#include "dbus/base/progress_adaptor_server.hpp"
#include "price_stream/adaptor/scheduled_task_adaptor.hpp"

namespace keep_my_journal {
using dbus_progress_struct_t = dbus::adaptor::dbus_progress_struct_t;

bool schedule_new_progress_task_impl(scheduled_price_task_t const &task);
void remove_scheduled_progress_task_impl(std::string const &user_id,
                                         std::string const &task_id);
std::vector<dbus_progress_struct_t>
get_scheduled_tasks_for_user_impl(std::string const &user_id);
std::vector<dbus_progress_struct_t> get_all_scheduled_tasks_impl();

class progress_based_task_dbus_server_t final
    : public sdbus::AdaptorInterfaces<
          keep::my::journal::interface::Progress_adaptor> {
public:
  progress_based_task_dbus_server_t(sdbus::IConnection &connection,
                                    std::string objectPath)
      : sdbus::AdaptorInterfaces<
            keep::my::journal::interface::Progress_adaptor>(
            connection, std::move(objectPath)) {
    registerAdaptor();
  }
  ~progress_based_task_dbus_server_t() { unregisterAdaptor(); }

  bool schedule_new_progress_task(dbus_progress_struct_t const &task) override {
    return schedule_new_progress_task_impl(
        dbus::adaptor::dbus_progress_to_scheduled_task(task));
  }

  void remove_scheduled_progress_task(std::string const &user_id,
                                      std::string const &task_id) final {
    remove_scheduled_progress_task_impl(user_id, task_id);
  }
  std::vector<dbus_progress_struct_t>
  get_scheduled_tasks_for_user(std::string const &user_id) final {
    return get_scheduled_tasks_for_user_impl(user_id);
  }
  std::vector<dbus_progress_struct_t> get_all_scheduled_tasks() final {
    return get_all_scheduled_tasks_impl();
  }
};
} // namespace keep_my_journal