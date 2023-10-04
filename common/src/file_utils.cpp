#include "file_utils.hpp"
#include "json_utils.hpp"
#include <boost/algorithm/string.hpp>
// #include "config.hpp"

namespace keep_my_journal::utils {
void normalizePaths(std::string &str) {
  for (std::string::size_type i = 0; i != str.size(); ++i) {
    if (str[i] == '#') {
#ifdef _WIN32
      str[i] = '\\';
#else
      str[i] = '/';
#endif // _WIN32
    }
  }
}

void replaceSpecialChars(std::string &str) {
  for (std::string::size_type i = 0; i != str.size(); ++i) {
#ifdef _WIN32
    if (str[i] == '\\')
#else
    if (str[i] == '/')
#endif
      str[i] = '#';
  }
}

void removeFile(std::string &filename) {
  std::error_code ec{};
  normalizePaths(filename);
  if (std::filesystem::exists(filename))
    std::filesystem::remove(filename, ec);
}

bool createFileDirectory(std::filesystem::path const &path) {
  std::error_code ec{};
  auto f = std::filesystem::absolute(path.parent_path(), ec);
  if (ec)
    return false;
  ec = {};
  std::filesystem::create_directories(f, ec);
  return !ec;
}

/*
std::unique_ptr<jordan::db_config_t>
parseConfigFile(std::string const &filename, std::string const &config_name) {
  auto const file_content_object = read_object_json_file(filename);
  if (!file_content_object)
    return nullptr;

  auto const database_list_iter = file_content_object->find("database");
  if (database_list_iter == file_content_object->cend())
    return nullptr;

  try {
    auto const &database_list = database_list_iter->second.get<json::array_t>();

    for (auto const &config_data : database_list) {
      auto const temp_object = config_data.get<json::object_t>();
      auto const temp_object_iter = temp_object.find("type");
      if (temp_object_iter != temp_object.cend() &&
          temp_object_iter->second == config_name) {
        auto const db_data = temp_object.at("data").get<json::object_t>();
        auto db_config = std::make_unique<db_config_t>();

        // let's get out the compulsory field first
        db_config->db_username = db_data.at("username").get<json::string_t>();
        db_config->db_password = db_data.at("password").get<json::string_t>();
        if (db_config->db_password.find('@') != std::string::npos)
          boost::replace_all(db_config->db_password, "@", "\\@");

        db_config->db_dns = db_data.at("db_dns").get<json::string_t>();
        db_config->jwt_secret_key =
            file_content_object->at("jwt_token").get<json::string_t>();
        db_config->tg_bot_secret_key =
            file_content_object->at("tg_bot_token").get<json::string_t>();

        return db_config;
      }
    }
  } catch (std::exception const &) {
    //
  }
  return nullptr;
}
*/

} // namespace keep_my_journal::utils
