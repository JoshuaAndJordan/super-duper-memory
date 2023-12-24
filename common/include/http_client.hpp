#pragma once

#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <map>
#include <optional>

namespace keep_my_journal {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websock = beast::websocket;
namespace http = beast::http;
namespace ip = net::ip;

using error_callback_t = std::function<void(beast::error_code const &)>;
using success_callback_t = std::function<void(std::string const &)>;

enum class http_method_e {
  get,
  post,
  put,
};

struct signed_message_t {

  struct header_value_t {
    std::string key;
    std::string value;
  };
  header_value_t timestamp;
  header_value_t apiKey;
  header_value_t passPhrase;
  header_value_t secretKey;
  header_value_t apiVersion;
};

std::string method_string(http_method_e method);
} // namespace keep_my_journal