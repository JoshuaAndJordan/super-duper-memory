#include "dbus/price_result_adaptor.hpp"
#include "dbus/use_cases/telegram_proxy_client_impl.hpp"
#include "string_utils.hpp"

char const *const telegram_dbus_dest_path = "keep.my.journal.messaging.tg";
char const *telegram_dbus_object_path = "/keep/my/journal/messaging/telegram/1";

namespace keep_my_journal {
telegram_proxy_impl &telegram_dbus_client() {
  static telegram_proxy_impl proxy(telegram_dbus_dest_path,
                                   telegram_dbus_object_path);
  return proxy;
}

void price_result_stream_t::broadcast_progress_price_result(
    dbus_progress_task_result_t const &res) {
  auto const &task =
      dbus::adaptor::dbus_progress_to_scheduled_task(res.get<0>());
  std::vector<dbus::adaptor::dbus_instrument_type_t> const &instruments =
      res.get<1>();

  std::ostringstream ss;
  ss << "PROGRESS UPDATE\n==============\n";
  ss << "Trade: " << utils::tradeTypeToString(task.tradeType) << "\n\n";
  for (auto const &instrument : instruments) {
    ss << "Name: " << instrument.get<0>() << "\nPrice: " << instrument.get<1>()
       << "\n24hrChange: " << instrument.get<2>() << "\n\n";
  }
  telegram_dbus_client().send_new_telegram_text(5'935'771'643, ss.str());
}

void price_result_stream_t::broadcast_time_price_result(
    keep_my_journal::dbus_time_task_result_t const &res) {
  auto const &task = dbus::adaptor::dbus_time_to_scheduled_task(res.get<0>());
  std::vector<dbus::adaptor::dbus_instrument_type_t> const &tokens =
      res.get<1>();

  std::ostringstream ss;
  ss << "TIME UPDATE\n==========\n";
  ss << "Trade: " << utils::tradeTypeToString(task.tradeType) << "\n\n";
  for (auto const &instrument : tokens) {
    ss << "Name: " << instrument.get<0>() << "\nPrice: " << instrument.get<1>()
       << "\n24hrChange: " << instrument.get<2>() << "\n\n";
  }
  telegram_dbus_client().send_new_telegram_text(5'935'771'643, ss.str());
}
} // namespace keep_my_journal