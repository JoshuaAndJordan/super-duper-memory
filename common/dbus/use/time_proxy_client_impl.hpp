#pragma once

#include "dbus/base/time_proxy_client.hpp"
#include <sdbus-c++/ProxyInterfaces.h>

namespace keep_my_journal {
class time_proxy_impl
    : public sdbus::ProxyInterfaces<keep::my::journal::interface::Time_proxy> {
public:
  time_proxy_impl(std::string destination, std::string object_path)
      : sdbus::ProxyInterfaces<keep::my::journal::interface::Time_proxy>(
            std::move(destination), std::move(object_path)) {
    registerProxy();
  }
  ~time_proxy_impl() { unregisterProxy(); }
};
} // namespace keep_my_journal