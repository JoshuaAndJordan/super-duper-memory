#include "string_utils.hpp"

#include <ctime>
#include <openssl/md5.h>
#include <sstream>

namespace jordan::utils {
void ltrim(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
          }));
}

void rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](unsigned char ch) { return !std::isspace(ch); })
              .base(),
          s.end());
}

void trim(std::string &s) {
  ltrim(s);
  rtrim(s);
}

std::string ltrimCopy(std::string s) {
  ltrim(s);
  return s;
}

std::string rtrimCopy(std::string s) {
  rtrim(s);
  return s;
}

std::string trimCopy(std::string const &s) {
  std::string temp = s;
  trim(temp);
  return s;
}

std::string decodeUrl(boost::string_view const &encoded_string) {
  std::string src{};
  for (size_t i = 0; i < encoded_string.size();) {
    char const ch = encoded_string[i];
    if (ch != '%') {
      src.push_back(ch);
      ++i;
    } else {
      char c1 = encoded_string[i + 1];
      unsigned int localui1 = 0L;
      if ('0' <= c1 && c1 <= '9') {
        localui1 = c1 - '0';
      } else if ('A' <= c1 && c1 <= 'F') {
        localui1 = c1 - 'A' + 10;
      } else if ('a' <= c1 && c1 <= 'f') {
        localui1 = c1 - 'a' + 10;
      }

      char c2 = encoded_string[i + 2];
      unsigned int localui2 = 0L;
      if ('0' <= c2 && c2 <= '9') {
        localui2 = c2 - '0';
      } else if ('A' <= c2 && c2 <= 'F') {
        localui2 = c2 - 'A' + 10;
      } else if ('a' <= c2 && c2 <= 'f') {
        localui2 = c2 - 'a' + 10;
      }

      unsigned int ui = localui1 * 16 + localui2;
      src.push_back(ui);

      i += 3;
    }
  }

  return src;
}

void trimString(std::string &str) { trim(str); }
bool unixTimeToString(std::string &output, std::size_t const t,
                      char const *format) {
  auto currentTime = static_cast<std::time_t>(t);
#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif
  auto const tmT = std::localtime(&currentTime);

  if (!tmT)
    return std::string::npos;
  output.clear();
  output.resize(32);
  return std::strftime(output.data(), output.size(), format, tmT) != 0;
}

std::string stringViewToString(boost::string_view const &str_view) {
  std::string str{str_view.begin(), str_view.end()};
  trimString(str);
  return str;
}

std::string stringListToString(std::vector<boost::string_view> const &vec) {
  if (vec.empty())
    return {};
  std::string str{};
  for (std::size_t index = 0; index < vec.size() - 1; ++index) {
    str.append(vec[index].to_string() + ", ");
  }
  str.append(vec.back().to_string());
  return str;
}

void hexToChar(std::string &s, std::vector<char> const &data) {
  s.clear();
  for (char const i : data) {
    char szBuff[3] = "";
    sprintf(szBuff, "%02x",
            *reinterpret_cast<const unsigned char *>(&i) & 0xff);
    s += szBuff[0];
    s += szBuff[1];
  }
}

std::string md5Hash(std::string const &input_data) {
  std::vector<char> vMd5;
  vMd5.resize(16);

  MD5_CTX ctx;
  MD5_Init(&ctx);
  MD5_Update(&ctx, input_data.c_str(), input_data.size());
  MD5_Final((unsigned char *)&vMd5[0], &ctx);

  std::string sMd5;
  hexToChar(sMd5, vMd5);
  return sMd5;
}

void replaceIfStarts(std::string &str, std::string const &oldStr,
                     std::string const &newStr) {
  std::string::size_type pos = 0u;
  while ((pos = str.find(oldStr, pos)) != std::string::npos) {
    str.replace(pos, oldStr.length(), newStr);
    pos += newStr.length();
  }
}

bool isValidMobileNumber(std::string_view const number, std::string &buffer) {
  if (number.size() < 12 || number.size() > 13)
    return false;

  std::size_t from = 2;
  if (number[0] == '+') { // international format
    if (number.size() != 13)
      return false;
    if (number[1] != '6' && number[2] != '3')
      return false;
    from = 3;
    buffer = std::string(number);
  } else if (number[0] == '6') { // international format, without the +
    if (number.size() != 12)
      return false;
    if (number[1] != '3')
      return false;
    from = 2;
    buffer = "+" + std::string(number);
  } else
    return false;
  for (std::size_t index = from; index < number.length(); ++index) {
    if (number[index] < '0' || number[index] > '9') {
      buffer.clear();
      return false;
    }
  }
  return true;
}

std::string_view boostViewToStdStringView(boost::string_view view) {
  return std::string_view(view.data(), view.size());
}

std::string integerListToString(std::vector<uint32_t> const &vec) {
  std::ostringstream ss{};
  if (vec.empty())
    return {};
  for (std::size_t i = 0; i != vec.size() - 1; ++i)
    ss << vec[i] << ", ";

  ss << vec.back();
  return ss.str();
}

std::vector<boost::string_view> splitStringView(boost::string_view const &str,
                                                char const *delim) {
  std::size_t const delim_length = std::strlen(delim);
  std::size_t from_pos{};
  std::size_t index{str.find(delim, from_pos)};
  if (index == std::string::npos)
    return {str};
  std::vector<boost::string_view> result{};
  while (index != std::string::npos) {
    result.emplace_back(str.data() + from_pos, index - from_pos);
    from_pos = index + delim_length;
    index = str.find(delim, from_pos);
  }
  if (from_pos < str.length())
    result.emplace_back(str.data() + from_pos, str.size() - from_pos);
  return result;
}

void splitStringInto(std::vector<std::string> &result, std::string const &str,
                     std::string const &delim) {
  std::size_t const delimLength = delim.length();
  std::size_t fromPos{};
  std::size_t index{str.find(delim, fromPos)};
  if (index == std::string::npos)
    return;

  while (index != std::string::npos) {
    result.emplace_back(str.data() + fromPos, index - fromPos);
    fromPos = index + delimLength;
    index = str.find(delim, fromPos);
  }

  if (fromPos < str.length())
    result.emplace_back(str.data() + fromPos, str.size() - fromPos);
}

#ifdef CRYPTOLOG_USING_MSGPACK
std::string exchangesToString(exchange_e const exchange) {
  switch(exchange){
    case exchange_e::binance:
      return "binance";
    case exchange_e::kucoin:
      return "kucoin";
    case exchange_e::okex:
      return "okex";
    default:
      return "unknown";
  }
}

exchange_e stringToExchange(std::string const &exchangeName) {
  if (exchangeName == "binance")
    return exchange_e::binance;
  else if (exchangeName == "kucoin")
    return exchange_e::kucoin;
  else if (exchangeName == "okex")
    return exchange_e::okex;
  return exchange_e::total;
}

std::string tradeTypeToString(trade_type_e const tradeType) {
  switch(tradeType){
    case trade_type_e::futures:
      return "futures";
    case trade_type_e::spot:
      return "spot";
    case trade_type_e::swap:
      return "swap";
    default:
      return "";
  }
}

trade_type_e stringToTradeType(std::string const &str) {
  if (str == "futures" || str == "future")
    return trade_type_e::futures;
  else if (str == "spot")
    return trade_type_e::spot;
  else if (str == "swap")
    return trade_type_e::swap;
  return trade_type_e::total;
}
#endif

} // namespace jordan::utils
