#pragma once

#include <string>

namespace jordan {

class uri_t {
public:
  uri_t() {}
  uri_t(std::string const &url_s);

  std::string path() const;
  std::string host() const;
  std::string target() const;
  std::string protocol() const;

private:
  void parse(std::string const &);
  std::string m_host;
  std::string m_path;
  std::string m_protocol;
  std::string m_query;
};

} // namespace jordan
