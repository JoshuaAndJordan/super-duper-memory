#include "database_connector.hpp"

#include <spdlog/spdlog.h>
#include <sstream>
#include <thread>
#include "string_utils.hpp"

namespace jordan {
void log_sql_error(otl_exception const &exception) {
  spdlog::error("SQLError code: {}", exception.code);
  spdlog::error("SQLError stmt: {}", exception.stm_text);
  spdlog::error("SQLError state: {}", (char const *)exception.sqlstate);
  spdlog::error("SQLError msg: {}", (char const*)exception.msg);
}

void otl_datetime_to_string(std::string &result, otl_datetime const &date) {
  result =
      fmt::format("{}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", date.year,
                  date.month, date.day, date.hour, date.minute, date.second);
}

std::unique_ptr<database_connector_t> &
database_connector_t::s_get_db_connector() {
  static std::unique_ptr<database_connector_t> db_connector{};
  if (!db_connector) {
    otl_connect::otl_initialize(1);
    db_connector = std::make_unique<database_connector_t>();
  }
  return db_connector;
}

void database_connector_t::set_username(std::string const &username) {
  m_dbConfig.dbUsername = username;
}

void database_connector_t::set_password(std::string const &password) {
  m_dbConfig.dbPassword = password;
}

void database_connector_t::set_database_name(std::string const &dbName) {
  m_dbConfig.dbDns = dbName;
}

void database_connector_t::keep_sql_server_busy() {
  spdlog::info("keeping DB server busy");
  auto const loginInfo = fmt::format("{}/{}@{}", m_dbConfig.dbUsername,
                                     m_dbConfig.dbPassword, m_dbConfig.dbDns);
  std::thread sql_thread{[this, loginInfo] {
    while (true) {
      try {
        otl_cursor::direct_exec(m_otlConnector, "select 1", true);
      } catch (otl_exception const &exception) {
        log_sql_error(exception);
        m_otlConnector.logoff();
        m_otlConnector.rlogon(loginInfo.c_str());
        std::this_thread::sleep_for(std::chrono::seconds(1));
        continue;
      }
      std::this_thread::sleep_for(std::chrono::minutes(15));
    }
  }};
  sql_thread.detach();
}

bool database_connector_t::connect() {
  if (!m_dbConfig)
    throw std::runtime_error("configuration incomplete");

  if (m_isRunning)
    return m_isRunning;

  auto const loginStr = fmt::format("{}/{}@{}", m_dbConfig.dbUsername,
                                    m_dbConfig.dbPassword, m_dbConfig.dbDns);
  try {
    this->m_otlConnector.rlogon(loginStr.c_str());
    keep_sql_server_busy();
    m_isRunning = true;
    return m_isRunning;
  } catch (otl_exception const &exception) {
    log_sql_error(exception);
    return m_isRunning;
  }
}

std::string string_or_null(std::string const &date_str) {
  if (date_str.empty())
    return "NULL";
  return "'" + date_str + "'";
}

bool database_connector_t::is_valid_user(std::string const &username,
                                         std::string const &password_hash) {
  std::string const field =
      username.find('@') == std::string::npos ? "username" : "email";

  auto const sql_statement = fmt::format(
      "SELECT id FROM jd_users WHERE {}='{}' AND password_hash='{}'", field,
      username, password_hash);
  try {
    std::lock_guard<std::mutex> lock_g{m_dbMutex};
    otl_stream db_stream(2, sql_statement.c_str(), m_otlConnector);
    int64_t user_id = 0;
    if (!db_stream.eof()) {
      db_stream >> user_id;
      return user_id != 0;
    }
  } catch (otl_exception const &e) {
    log_sql_error(e);
  }
  return false;
}

bool database_connector_t::username_exists(std::string const &username) {
  if (m_usernames.find(username) != m_usernames.end())
    return true;

  auto const sql_statement =
      fmt::format("SELECT id FROM jb_users WHERE username='{}'", username);
  try {
    std::lock_guard<std::mutex> lock_g{m_dbMutex};
    otl_stream db_stream(1, sql_statement.c_str(), m_otlConnector);
    int user_id{};
    if (!db_stream.eof())
      db_stream >> user_id;

    bool const exists = user_id != 0;
    if (exists)
      m_usernames.insert(username);
    return exists;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
}

bool database_connector_t::email_exists(std::string const &email) {
  auto const sql_statement =
      fmt::format("SELECT id FROM jb_users WHERE email='{}'", email);
  try {
    std::lock_guard<std::mutex> lock_g{m_dbMutex};
    otl_stream db_stream(1, sql_statement.c_str(), m_otlConnector);
    int user_id{};
    if (!db_stream.eof())
      db_stream >> user_id;
    return user_id != 0;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
}

bool database_connector_t::add_new_user(user_registration_data_t const &data) {
  auto const sql_statement =
      fmt::format("INSERT INTO jd_users(first_name, last_name, address,"
                  "email, username, password_hash) VALUES("
                  "'{}', '{}', '{}', '{}', '{}', '{}')",
                  data.firstName, data.lastName, data.address, data.email,
                  data.username, data.passwordHash);
  std::lock_guard<std::mutex> lock_g{m_dbMutex};
  try {
    otl_cursor::direct_exec(m_otlConnector, sql_statement.c_str(),
                            otl_exception::enabled);
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
  return true;
}

int database_connector_t::add_new_monitor_task(account_scheduled_task_t const &task) {
  int insertID = -1;

  std::string todayDate {};
  auto const now = std::time(nullptr);
  if (!utils::unixTimeToString(todayDate, now))
    return insertID;

  auto sqlStatement =
      fmt::format("INSERT INTO jd_monitor_accounts(user_id, api_key, "
                  "secret_key, passphrase, exchange_name, trade_type,"
                  "task_status, date_added, date_updated) VALUES ("
                  "{}, '{}', '{}', '{}', '{}', '{}', {}, '{}', '{}')",
                  task.userID, task.apiKey, task.secretKey, task.passphrase,
                  utils::exchangesToString(task.exchange),
                  (int)task.tradeType, (int) task_state_e::initiated,
                  todayDate, todayDate);

  std::lock_guard<std::mutex> lockG(m_dbMutex);
  try {
    otl_cursor::direct_exec(m_otlConnector, sqlStatement.c_str(),
                            otl_exception::enabled);
    // get the ID of the last inserted data
    sqlStatement = "SELECT MAX(ID) FROM jd_monitor_accounts";
    otl_stream db_stream(1, sqlStatement.c_str(), m_otlConnector);
    if (!db_stream.eof())
      db_stream >> insertID;
  } catch (otl_exception const &e) {
    log_sql_error(e);
  }
  return insertID;
}

bool database_connector_t::change_monitor_task_status(
    int64_t const userID, int const taskID, task_state_e const status) {
  auto const sqlStatement =
      fmt::format("UPDATE jb_monitor_accounts SET task_status={} WHERE "
                  "userID={} AND taskID={}", (int)status, userID, taskID);
  try {
    otl_cursor::direct_exec(m_otlConnector, sqlStatement.c_str(),
                            otl_exception::enabled);
  } catch(otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
  return true;
}

bool database_connector_t::remove_monitor_task(
    int64_t const userID, int64_t const taskID) {
  auto const sqlStatement =
      fmt::format("DELETE FROM jb_monitor_accounts WHERE user_id={} AND "
                  "taskID={}", userID, taskID);
  try {
    otl_cursor::direct_exec(m_otlConnector, sqlStatement.c_str(),
                            otl_exception::enabled);
  } catch(otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
  return true;
}

} // namespace jordan
