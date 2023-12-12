#include "crypto_utils.hpp"

#include <limits>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/params.h>
#include <stdexcept>
#include <vector>

namespace keep_my_journal::utils {

std::string base64Encode(std::basic_string<unsigned char> const &bindata) {
  static const char b64_table[65] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  if (bindata.size() >
      (std::numeric_limits<std::string::size_type>::max() / 4u) * 3u) {
    throw std::length_error("Converting too large a string to base64.");
  }

  std::size_t const binlen = bindata.size();
  // Use = signs so the end is properly padded.
  std::string retval((((binlen + 2) / 3) * 4), '=');
  std::size_t outpos{};
  int bits_collected{};
  unsigned int accumulator{};

  for (auto const &i : bindata) {
    accumulator = (accumulator << 8) | (i & 0xffu);
    bits_collected += 8;
    while (bits_collected >= 6) {
      bits_collected -= 6;
      retval[outpos++] = b64_table[(accumulator >> bits_collected) & 0x3fu];
    }
  }

  if (bits_collected > 0) { // Any trailing bits that are missing.
    if (bits_collected >= 6)
      return {};
    accumulator <<= 6 - bits_collected;
    retval[outpos++] = b64_table[accumulator & 0x3fu];
  }
  if (outpos >= (retval.size() - 2) && (outpos <= retval.length()))
    return retval;
  return {};
}

std::string base64Encode(std::string const &bindata) {
  std::basic_string<unsigned char> temp{};
  temp.resize(bindata.size());
  for (int i = 0; i < bindata.size(); ++i)
    temp[i] = static_cast<unsigned char>(bindata[i]);
  return base64Encode(temp);
}

std::string base64Decode(std::string const &asc_data) {
  static char const reverse_table[128]{
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
      52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
      64, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
      15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
      64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
      41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64};
  std::string ret_val{};
  int bits_collected{};
  unsigned int accumulator{};

  for (auto const &c : asc_data) {
    if (::std::isspace(c) || c == '=') {
      // Skip whitespace and padding. Be liberal in what you accept.
      continue;
    }
    if ((c > 127) || (c < 0) || (reverse_table[c] > 63)) {
      throw std::invalid_argument(
          "This contains characters not legal in a base64 encoded string.");
    }
    accumulator = (accumulator << 6) | reverse_table[c];
    bits_collected += 6;
    if (bits_collected >= 8) {
      bits_collected -= 8;
      ret_val += static_cast<char>((accumulator >> bits_collected) & 0xFFu);
    }
  }
  return ret_val;
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

std::basic_string<unsigned char> hmac256Encode(std::string const &data,
                                               std::string const &key) {
  struct mac_deleter_t {
    EVP_MAC **m_mac = nullptr;
    EVP_MAC_CTX **m_ctx = nullptr;
    mac_deleter_t(EVP_MAC **mac, EVP_MAC_CTX **ctx) : m_mac(mac), m_ctx(ctx) {}
    ~mac_deleter_t() {
      if (*m_mac)
        EVP_MAC_free(*m_mac);
      if (*m_ctx)
        EVP_MAC_CTX_free(*m_ctx);
    }
    static int openssl_dummy() {
      OpenSSL_add_all_digests();
      return 0;
    }
  };

  static int const dummyValue = mac_deleter_t::openssl_dummy();
  EVP_MAC *mac = nullptr;
  EVP_MAC_CTX *ctx = nullptr;
  OSSL_PARAM params[2] = {
      OSSL_PARAM_construct_utf8_string("digest", (char *)"SHA256", 0),
      OSSL_PARAM_construct_end()};
  mac_deleter_t macDeleter(&mac, &ctx);

  mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
  if (!mac)
    throw std::runtime_error("Unable to fetch SHA256 MAC");

  ctx = EVP_MAC_CTX_new(mac);
  if (!ctx)
    throw std::runtime_error("Unable to create MAC context");

  if (!EVP_MAC_init(ctx, (const unsigned char *)key.c_str(), key.length(),
                    params)) {
    throw std::runtime_error("Unable to initialize MAC context with key");
  }

  size_t len{};
  unsigned char out[EVP_MAX_MD_SIZE];
  EVP_MAC_update(ctx, (unsigned char *)data.c_str(), data.length());
  EVP_MAC_final(ctx, out, &len, EVP_MAX_MD_SIZE);
  (void)dummyValue;

  return std::basic_string<unsigned char>(out, len);
}
} // namespace keep_my_journal::utils
