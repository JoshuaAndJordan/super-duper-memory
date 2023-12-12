#include "file_utils.hpp"
#include "db_config.hpp"
#include "json_utils.hpp"
#include "spdlog/spdlog.h"
#include <boost/algorithm/string.hpp>

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

std::optional<json::object_t>
read_object_json_file(std::string const &filename) {
  std::ifstream fileStream(filename);
  if (!(std::filesystem::exists(filename) && fileStream))
    return std::nullopt;

  fileStream.seekg(0, std::ios::end);
  int const bufferSize = static_cast<int>(fileStream.tellg());
  fileStream.seekg(0, std::ios::beg);

  std::unique_ptr<char[]> buffer(new char[bufferSize + 1]);
  fileStream.read(buffer.get(), bufferSize);

  return json::parse(std::string_view(buffer.get(), bufferSize))
      .get<json::object_t>();
}

void log_general_exception(std::exception const &e);

std::unique_ptr<db_config_t> parseConfigFile(std::string const &filename,
                                             std::string const &config_name) {
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
        db_config->dbUsername = db_data.at("username").get<json::string_t>();
        db_config->dbPassword = db_data.at("password").get<json::string_t>();
        if (db_config->dbPassword.find('@') != std::string::npos)
          boost::replace_all(db_config->dbPassword, "@", "\\@");

        db_config->dbDns = db_data.at("db_dns").get<json::string_t>();
        db_config->jwtSecretKey =
            file_content_object->at("jwt_token").get<json::string_t>();
        return db_config;
      }
    }
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
  return nullptr;
}

} // namespace keep_my_journal::utils
