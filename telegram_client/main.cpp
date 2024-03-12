#include "include/telegram_adaptor_server_impl.hpp"
#include <boost/type_index/ctti_type_index.hpp>
#include <functional>
#include <iostream>
#include <map>
#include <sdbus-c++/sdbus-c++.h>
#include <spdlog/spdlog.h>
#include <td/telegram/Client.h>
#include <thread>

namespace keep_my_journal::tg {
namespace td_api = td::td_api;

using response_ptr_t = std::shared_ptr<td::Client::Response>;
using function_ptr_t = td_api::object_ptr<td_api::Function>;
using object_ptr_t = td_api::object_ptr<td_api::Object>;
using custom_request_handler_t = std::function<void(object_ptr_t)>;
using custom_request_handler_map_t = std::map<size_t, custom_request_handler_t>;

namespace detail {
template <class... Fs> struct overload;

template <class F> struct overload<F> : public F {
  explicit overload(F f) : F(f) {}
};

template <class F, class... Fs>
struct overload<F, Fs...> : public overload<F>, overload<Fs...> {
  explicit overload(F f, Fs... fs) : overload<F>(f), overload<Fs...>(fs...) {}

  using overload<F>::operator();
  using overload<Fs...>::operator();
};
} // namespace detail

template <class... F> auto overloaded(F... f) {
  return detail::overload<F...>(f...);
}

constexpr const auto AppID = 1'127'150;

class telegram_class_t {
  void process_update(object_ptr_t ptr);
  void process_response(response_ptr_t response);
  void on_authorization_state_update();
  void requested_authorization_code();
  void requested_authorization_password();
  void requested_phone_number();
  void requested_app_parameters();
  void handshake_completed();
  void restart();
  void send_request(uint64_t query_id, function_ptr_t request,
                    custom_request_handler_t handler);
  custom_request_handler_t create_authentication_handler();
  static uint64_t next_id();
  static void check_authentication_error(object_ptr_t);

public:
  explicit telegram_class_t(std::string phone_number)
      : m_phoneNumber(std::move(phone_number)),
        m_client(std::make_shared<td::Client>()) {}

  int run();
  void on_new_authorization_code(std::string const &, std::string const &);
  void on_new_authorization_password(std::string const &, std::string const &);
  std::map<long, td_api::object_ptr<td_api::user>> m_users;

private:
  bool m_authorizationGranted = false;
  bool m_errorIsSet = false;
  bool m_needsRestart = false;
  size_t m_authenticationQueryID = 0;
  std::string const m_phoneNumber;
  std::map<std::int64_t, std::string> m_chatTitle;
  std::map<std::int64_t, std::string> m_groupNames;
  std::shared_ptr<td::Client> m_client = nullptr;
  custom_request_handler_map_t m_requestHandlers;
  td_api::object_ptr<td_api::AuthorizationState> m_authorizationState = nullptr;
};

uint64_t telegram_class_t::next_id() {
  static std::uint64_t id = 0;
  return ++id;
}

void telegram_class_t::restart() {
  m_authorizationGranted = m_errorIsSet = m_needsRestart = false;
  m_requestHandlers.clear();
  m_client = std::make_shared<td::Client>();
}

custom_request_handler_t telegram_class_t::create_authentication_handler() {
  return [this, id = m_authenticationQueryID](object_ptr_t object) {
    if (id == m_authenticationQueryID)
      check_authentication_error(std::move(object));
  };
}

void telegram_class_t::check_authentication_error(object_ptr_t object) {
  if (object->get_id() != td_api::error::ID)
    return spdlog::error("{} failed", __func__);
  auto error = to_string(td::move_tl_object_as<td_api::error>(object));
  std::cerr << error << std::endl;
}

void telegram_class_t::process_response(response_ptr_t response) {
  spdlog::info("let's see this {}", __func__);
  if (!response->object)
    return spdlog::info("Returning from {}", __func__);

  spdlog::info("Response ID: {}", response->id);
  static int const initial_request = 0;
  if (response->id == initial_request)
    return process_update(std::move(response->object));

  auto it = m_requestHandlers.find(response->id);
  if (it != m_requestHandlers.end())
    return it->second(std::move(response->object));
  spdlog::error("Nothing found in m_requestHandlers");
}

void telegram_class_t::handshake_completed() {
  spdlog::info("Handshake completed -> {}", m_phoneNumber);
}

void telegram_class_t::send_request(
    uint64_t const query_id, function_ptr_t request,
    keep_my_journal::tg::custom_request_handler_t handler) {
  if (handler)
    m_requestHandlers.emplace(query_id, std::move(handler));
  m_client->send({query_id, std::move(request)});
}

void telegram_class_t::on_authorization_state_update() {
  ++m_authenticationQueryID;
  td_api::downcast_call(
      *m_authorizationState,
      overloaded(
          [this](td_api::authorizationStateReady &) {
            spdlog::info("td_api::authorizationStateReady &");
            m_authorizationGranted = true;
          },
          [this](td_api::authorizationStateLoggingOut &) {
            spdlog::info("td_api::authorizationStateLoggingOut &");
            m_authorizationGranted = false;
          },
          [this](td_api::authorizationStateClosing &) {
            spdlog::info("td_api::authorizationStateClosing &");
            m_authorizationGranted = false;
          },
          [this](td_api::authorizationStateClosed &) {
            spdlog::info("td_api::authorizationStateClosed &");
            m_authorizationGranted = false;
            m_needsRestart = true;
          },
          [this](td_api::authorizationStateWaitCode &) {
            requested_authorization_code();
          },
          [](td_api::authorizationStateWaitOtherDeviceConfirmation &) {
            spdlog::info(
                "td_api::authorizationStateWaitOtherDeviceConfirmation &");
          },
          [](td_api::authorizationStateWaitRegistration &) {
            spdlog::info("td_api::authorizationStateWaitRegistration &");
          },
          [this](td_api::authorizationStateWaitEncryptionKey &) {
            spdlog::info("td_api::authorizationStateWaitEncryptionKey &");
            auto key = td_api::make_object<td_api::setDatabaseEncryptionKey>();
            key->new_encryption_key_ = "dsfoisdifpsdipfhwerow4r49weQUIDIQWDB!";
            send_request(next_id(), std::move(key),
                         create_authentication_handler());
          },
          [this](td_api::authorizationStateWaitPassword &) {
            requested_authorization_password();
          },
          [this](td_api::authorizationStateWaitPhoneNumber &) {
            requested_phone_number();
          },
          [this](td_api::authorizationStateWaitTdlibParameters &) {
            requested_app_parameters();
          }));
}

void telegram_class_t::process_update(object_ptr_t ptr) {
  td_api::downcast_call(
      *ptr,
      overloaded(
          [this](td_api::updateAuthorizationState &update_authorization_state) {
            spdlog::info("td_api::updateAuthorizationState &");
            m_authorizationState =
                std::move(update_authorization_state.authorization_state_);
            on_authorization_state_update();
          },
          [this](td_api::updateUser &update_user) {
            spdlog::info("td_api::updateUser &");
            auto user_id = update_user.user_->id_;
            m_users[user_id] = std::move(update_user.user_);
          },
          [this](td_api::updateNewChat &chat) {
            spdlog::info("td_api::updateNewChat &");
            int const chat_type_id = chat.chat_->type_->get_id();
            static int const big_group = 955'152'366;
            static int const small_group = 21'815'278;
            auto &chat_obj = chat.chat_;

            if (chat_type_id == big_group || chat_type_id == small_group)
              m_groupNames[chat_obj->id_] = chat_obj->title_;
            m_chatTitle[chat_obj->id_] = chat_obj->title_;
          },
          [=](auto &a) {
            using type_t = std::remove_cv_t<std::decay_t<decltype(a)>>;
            boost::typeindex::ctti_type_index sti =
                boost::typeindex::ctti_type_index::type_id<type_t>();
            spdlog::info("Name: {}", sti.pretty_name());
          }));
}

int telegram_class_t::run() {
  spdlog::info("{} called to start", __func__);
  td::Client::execute(
      {0, td::td_api::make_object<td::td_api::setLogVerbosityLevel>(1)});

  while (!m_authorizationGranted) {
    if (m_errorIsSet) {
      spdlog::error("Error is set");
      break;
    }
    if (m_needsRestart) {
      spdlog::info("This needs a restart...");
      restart();
    }
    auto response =
        std::make_shared<td::Client::Response>(m_client->receive(10));
    if (!response->object) {
      m_errorIsSet = true;
      break;
    }
    process_response(response);
  }
  handshake_completed();
  return EXIT_SUCCESS;
}

void telegram_class_t::on_new_authorization_code(
    std::string const &mobile_number, std::string const &code) {
  if (mobile_number != m_phoneNumber)
    return;
  spdlog::info("Code called now: {}", code);
  send_request(next_id(),
               td_api::make_object<td_api::checkAuthenticationCode>(code),
               create_authentication_handler());
}

void telegram_class_t::on_new_authorization_password(
    std::string const &mobile_number, std::string const &password) {
  if (mobile_number != m_phoneNumber)
    return;
  send_request(
      next_id(),
      td_api::make_object<td_api::checkAuthenticationPassword>(password),
      create_authentication_handler());
}

void telegram_class_t::requested_authorization_code() {
  spdlog::info("{} called", __func__);
}

void telegram_class_t::requested_authorization_password() {
  spdlog::info("{} called", __func__);
}

void telegram_class_t::requested_phone_number() {
  spdlog::info("{} called", __func__);
  send_request(next_id(),
               td_api::make_object<td_api::setAuthenticationPhoneNumber>(
                   m_phoneNumber, nullptr),
               create_authentication_handler());
}

void telegram_class_t::requested_app_parameters() {
  spdlog::info("{} called", __func__);
  auto parameters = td_api::make_object<td_api::tdlibParameters>();
  parameters->database_directory_ = m_phoneNumber;
  parameters->use_message_database_ = true;
  parameters->use_secret_chats_ = true;
  parameters->api_id_ = AppID;
  parameters->api_hash_ = "7ea9bdf786f0fd19bf511edef0159e4c";
  parameters->system_language_code_ = "en";
  parameters->device_model_ = "Desktop";
  parameters->system_version_ = "Windows 12";
  parameters->application_version_ = "1.6";
  send_request(
      next_id(),
      td_api::make_object<td_api::setTdlibParameters>(std::move(parameters)),
      create_authentication_handler());
}

telegram_class_t &get_instance() {
  static std::string const phone_number = "+447585291678";
  static keep_my_journal::tg::telegram_class_t tg(phone_number);
  return tg;
}
} // namespace keep_my_journal::tg

namespace keep_my_journal {
void on_authorization_code_requested_impl(std::string const &mobile_number,
                                          std::string const &code) {
  spdlog::info("{} called with param: {} -> {}", __func__, mobile_number, code);
  tg::get_instance().on_new_authorization_code(mobile_number, code);
}

void on_authorization_password_requested_impl(std::string const &mobile_number,
                                              std::string const &password) {
  spdlog::info("{} called with param: {} -> {}", __func__, mobile_number,
               password);
  tg::get_instance().on_new_authorization_password(mobile_number, password);
}

} // namespace keep_my_journal

int main() {
  auto &instance = keep_my_journal::tg::get_instance();
  std::thread{[&instance] { instance.run(); }}.detach();
  char const *service_name = "keep.my.journal.messaging.tg";
  char const *object_path = "/keep/my/journal/messaging/telegram/1";
  auto dbus_connection = sdbus::createSystemBusConnection(service_name);
  keep_my_journal::telegram_adaptor_server_impl proxy(*dbus_connection,
                                                      object_path);
  dbus_connection->enterEventLoop();
  return EXIT_SUCCESS;
}
