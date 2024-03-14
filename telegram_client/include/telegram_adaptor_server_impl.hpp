#pragma once

#include "dbus/base/telegram_adaptor_server.hpp"

namespace keep_my_journal {
void on_authorization_code_requested_impl(std::string const &,
                                          std::string const &);
void on_authorization_password_requested_impl(std::string const &,
                                              std::string const &);
void send_new_telegram_text_impl(int64_t, std::string const &);

using telegram_base_t =
    sdbus::AdaptorInterfaces<keep::my::journal::messaging::tg_adaptor>;

class telegram_adaptor_server_impl final : public telegram_base_t {
public:
  telegram_adaptor_server_impl(sdbus::IConnection &connection,
                               std::string object_path)
      : telegram_base_t(connection, std::move(object_path)) {
    registerAdaptor();
  }
  ~telegram_adaptor_server_impl() { unregisterAdaptor(); }
  void on_authorization_code_requested(std::string const &mobile_number,
                                       std::string const &code) final {
    return on_authorization_code_requested_impl(mobile_number, code);
  }

  void on_authorization_password_requested(std::string const &mobile_number,
                                           std::string const &password) final {
    return on_authorization_password_requested_impl(mobile_number, password);
  }

  void send_new_telegram_text(int64_t const &chat_id,
                              std::string const &content) final {
    return send_new_telegram_text_impl(chat_id, content);
  }
};
} // namespace keep_my_journal