// Copyright (C) 2023-2024 Joshua and Jordan Ogunyinka

#include "dbus/price_result_adaptor.hpp"
#include <sdbus-c++/sdbus-c++.h>

int main() {
  char const *const service_name = "keep.my.journal.prices.result";
  char const *object_path = "/keep/my/journal/prices/result/1";
  auto dbus_connection = sdbus::createSystemBusConnection(service_name);
  keep_my_journal::price_result_stream_t dbus_server(*dbus_connection,
                                                     object_path);
  dbus_connection->enterEventLoop();
  return EXIT_SUCCESS;
}
