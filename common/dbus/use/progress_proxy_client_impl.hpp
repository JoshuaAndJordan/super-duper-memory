#pragma once

#include "dbus/base/progress_proxy_client.hpp"
#include <sdbus-c++/ProxyInterfaces.h>

namespace keep_my_journal {
class progress_proxy_impl : public sdbus::ProxyInterfaces<
                                keep::my::journal::interface::Progress_proxy> {
public:
  progress_proxy_impl(std::string destination, std::string object_path)
      : sdbus::ProxyInterfaces<keep::my::journal::interface::Progress_proxy>(
            std::move(destination), std::move(object_path)) {
    registerProxy();
  }
  ~progress_proxy_impl() { unregisterProxy(); }
};

} // namespace keep_my_journal