#include "dbus/progress_task_adaptor.hpp"

using keep_my_journal::instrument_exchange_set_t;
instrument_exchange_set_t uniqueInstruments{};

namespace keep_my_journal {
void monitor_tokens_latest_prices(bool &isRunning);
}

int main() {
  bool isRunning = true;
  // connect to the price watching process and get the latest prices from the
  // price_stream
  std::thread{[&isRunning] {
    // defined in latest_prices_watcher.cpp
    keep_my_journal::monitor_tokens_latest_prices(isRunning);
  }}.detach();

  char const *service_name = "keep.my.journal.progress";
  char const *object_path = "/keep/my/journal/progress/1";
  auto dbus_connection = sdbus::createSystemBusConnection(service_name);
  keep_my_journal::progress_based_task_dbus_server_t progress_proxy(
      *dbus_connection, object_path);
  dbus_connection->enterEventLoop();
  return 0;
}
