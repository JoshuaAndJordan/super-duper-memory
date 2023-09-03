// openssl_test.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
//

#include <iostream>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/params.h>

std::basic_string<char> hmac256Encode(std::string const &data,
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

  return std::basic_string<char>((char *)out, len);
}

int main() {
  auto const hmacString = hmac256Encode("Joshua", "Jordan");
  std::cout << hmacString << std::endl;
  return 0;
}
