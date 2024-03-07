#include "dbus/time_task_adaptor.hpp"

using keep_my_journal::instrument_exchange_set_t;
instrument_exchange_set_t uniqueInstruments{};

namespace keep_my_journal {
void monitor_tokens_latest_prices(bool &isRunning);
void result_sender_callback(bool &);
} // namespace keep_my_journal

int main() {
  bool isRunning = true;
  // connect to the price watching process and get the latest prices from the
  // price_stream
  std::thread{[&isRunning] {
    // defined in latest_prices_watcher.cpp
    keep_my_journal::monitor_tokens_latest_prices(isRunning);
  }}.detach();

  std::thread{[&isRunning] {
    // defined in time_based_watch.cpp
    keep_my_journal::result_sender_callback(isRunning);
  }}.detach();

  char const *const service_name = "keep.my.journal.time";
  char const *object_path = "/keep/my/journal/time/1";
  auto dbus_connection = sdbus::createSystemBusConnection(service_name);
  keep_my_journal::time_based_task_dbus_server_t proxy(*dbus_connection,
                                                       object_path);
  dbus_connection->enterEventLoop();
  return 0;
}