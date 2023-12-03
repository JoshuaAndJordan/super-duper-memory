// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

#define OTL_BIG_INT long long
#define OTL_ODBC_MYSQL
#define OTL_STL
#define OTL_STREAM_WITH_STD_TUPLE_ON
#ifdef _WIN32
#define OTL_ODBC_WINDOWS
#else
#define OTL_ODBC_UNIX
#endif

#define OTL_SAFE_EXCEPTION_ON
#include "account_stream/user_scheduled_task.hpp"
#include "db_config.hpp"
#include "otl_v4/otlv4.h"
#include "scheduled_price_tasks.hpp"
#include "user_info.hpp"

namespace keep_my_journal {

void log_sql_error(otl_exception const &exception);
std::string
price_tasks_to_db_string(std::vector<scheduled_price_task_t> const &tasks);

class database_connector_t {
  std::set<std::string> m_usernames{};

  db_config_t m_dbConfig;
  otl_connect m_otlConnector;
  std::mutex m_dbMutex;
  bool m_isRunning = false;

private:
  void keep_sql_server_busy();

public:
  static std::unique_ptr<database_connector_t> &s_get_db_connector();
  bool connect();
  void set_username(std::string const &username);
  void set_password(std::string const &password);
  void set_database_name(std::string const &db_name);

  [[nodiscard]] bool username_exists(std::string const &username);
  [[nodiscard]] bool email_exists(std::string const &email);
  [[nodiscard]] bool add_new_user(user_registration_data_t const &);
  [[nodiscard]] int64_t is_valid_user(std::string const &username,
                                      std::string const &passwordHash);
  [[nodiscard]] int add_new_monitor_task(account_scheduled_task_t const &);
  [[nodiscard]] bool change_monitor_task_status(int64_t userID, int taskID,
                                                task_state_e);
  [[nodiscard]] bool remove_monitor_task(int64_t userID, int64_t taskID);
  [[nodiscard]] int add_new_price_task(scheduled_price_task_t const &);
  [[nodiscard]] std::vector<int>
  add_price_tasks_or_abort(std::vector<scheduled_price_task_t> const &);
  [[nodiscard]] std::vector<scheduled_price_task_t>
  list_pricing_tasks(int64_t userID);
  void remove_price_task(int taskID, int64_t userID);
  void remove_price_tasks(std::vector<scheduled_price_task_t> const &);
};
} // namespace keep_my_journal
