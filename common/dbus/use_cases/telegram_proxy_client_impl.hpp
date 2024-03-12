#pragma once

#include "dbus/base/telegram_proxy_client.hpp"

namespace keep_my_journal {
class telegram_proxy_impl
    : public sdbus::ProxyInterfaces<keep::my::journal::messaging::tg_proxy> {
public:
  telegram_proxy_impl(std::string destination, std::string object_path)
      : sdbus::ProxyInterfaces<keep::my::journal::messaging::tg_proxy>(
            std::move(destination), std::move(object_path)) {
    registerProxy();
  }
  ~telegram_proxy_impl() { unregisterProxy(); }
};
} // namespace keep_my_journal