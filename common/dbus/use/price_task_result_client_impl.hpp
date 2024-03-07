#pragma once

#include "dbus/base/price_task_result_client.hpp"
#include <sdbus-c++/ProxyInterfaces.h>

namespace keep_my_journal {
class prices_result_proxy_impl_t
    : public sdbus::ProxyInterfaces<
          keep::my::journal::prices::interface::result_proxy> {
public:
  prices_result_proxy_impl_t(std::string destination, std::string object_path)
      : sdbus::ProxyInterfaces<
            keep::my::journal::prices::interface::result_proxy>(
            std::move(destination), std::move(object_path)) {
    registerProxy();
  }
  ~prices_result_proxy_impl_t() { unregisterProxy(); }
};

} // namespace keep_my_journal