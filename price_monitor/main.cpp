#define _CRT_SECURE_NO_WARNINGS

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

namespace net = boost::asio;
namespace ssl = net::ssl;

namespace jordan {
// all functions here are implemented in each exchanges' prices source
void binance_price_watcher(net::io_context &, ssl::context &);
void okexchange_price_watcher(net::io_context &, ssl::context &);

// to be implemented
void kucoin_price_watcher(net::io_context &, ssl::context &) {}
} // namespace jordan

int main(int argc, char *argv[]) {
  int const native_thread_size = std::thread::hardware_concurrency();
  net::io_context ioContext{native_thread_size};
  boost::asio::ssl::context sslContext(
      boost::asio::ssl::context::tlsv12_client);
  sslContext.set_default_verify_paths();
  char const *dir = std::getenv(X509_get_default_cert_dir_env());
  if (!dir)
    dir = X509_get_default_cert_dir();

  if (auto const verify_file = std::filesystem::path(dir) / "ca-bundle.crt";
      dir && std::filesystem::exists(verify_file)) {
    sslContext.set_verify_mode(ssl::verify_peer);
    sslContext.load_verify_file(verify_file.string());
  } else {
    sslContext.set_verify_mode(ssl::verify_none);
  }

  std::thread binanceWatcher{[&ioContext, &sslContext] {
    jordan::binance_price_watcher(ioContext, sslContext);
  }};
  
  std::thread kucoinWatcher{[&ioContext, &sslContext] {
    jordan::kucoin_price_watcher(ioContext, sslContext);
  }};

  std::thread okWatcher{[&ioContext, &sslContext] {
    jordan::okexchange_price_watcher(ioContext, sslContext);
  }};

  // wait a bit for all tasks to start up and have async "actions" lined up
  binanceWatcher.join();
  kucoinWatcher.join();
  okWatcher.join();

  ioContext.run();

  std::cout << "Done running stuff..." << std::endl;
  return EXIT_SUCCESS;
}
