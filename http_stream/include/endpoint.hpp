#pragma once

#include <boost/beast/http/verb.hpp>
#include <boost/utility/string_view.hpp>
#include <functional>
#include <map>
#include <optional>
#include <string>

#define BN_REQUEST_PARAM (url_query_t const &optional_query)

namespace http = boost::beast::http;

namespace keep_my_journal {
namespace utils {
std::string trimCopy(std::string const &);
}

using url_query_t = std::map<std::string, std::string>;
using callback_t = std::function<void(url_query_t const &)>;

struct rule_t {
  std::vector<http::verb> verbs{};
  callback_t route_callback;

  template <typename Verb, typename... Verbs>
  rule_t(callback_t callback, Verb &&verb, Verbs &&...verbs)
      : verbs{std::forward<Verb>(verb), std::forward<Verbs>(verbs)...},
        route_callback{std::move(callback)} {}
};

class endpoint_t {
  friend class session_t;
  struct special_placeholders_t {
    struct key_value_pair_t {
      std::string key{};
      std::string value{};
    };
    std::vector<key_value_pair_t> placeholders;
    std::optional<rule_t> rule = std::nullopt;
    std::string suffix;

    special_placeholders_t() = default;
    template <typename Verb, typename... Verbs>
    special_placeholders_t(callback_t &&cb, Verb &&verb, Verbs &&...verbs)
        : rule{std::in_place, std::move(cb), std::forward<Verb>(verb),
               std::forward<Verbs>(verbs)...} {}
  };

  std::map<std::string, rule_t> m_endpoints;
  std::map<std::string, special_placeholders_t> m_specialEndpoints;
  using rule_iterator = std::map<std::string, rule_t>::iterator;

  void construct_special_placeholder(special_placeholders_t &,
                                     std::string const &);

public:
  template <typename Verb, typename... Verbs>
  void add_endpoint(std::string route, callback_t &&cb, Verb &&verb,
                    Verbs &&...verbs) {
    if (route.empty() || route[0] != '/')
      throw std::runtime_error{"A valid route starts with a /"};
    while (route.back() == '/')
      route.pop_back();
    m_endpoints.emplace(route, rule_t{std::move(cb), std::forward<Verb>(verb),
                                      std::forward<Verbs>(verbs)...});
  }

  template <typename Verb, typename... Verbs>
  void add_special_endpoint(std::string const &route, callback_t &&cb,
                            Verb &&verb, Verbs &&...verbs) {
    if (route.empty() || route[0] != '/')
      throw std::runtime_error{"A valid route starts with a /"};
    special_placeholders_t placeholder{std::move(cb), std::forward<Verb>(verb),
                                       std::forward<Verbs>(verbs)...};
    construct_special_placeholder(placeholder, route);
  }

  std::optional<rule_iterator> get_rules(std::string const &target);
  std::optional<rule_iterator> get_rules(boost::string_view const &target);
  std::optional<special_placeholders_t> get_special_rules(std::string);
};
} // namespace keep_my_journal