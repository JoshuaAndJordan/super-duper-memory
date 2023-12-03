// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/context.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

namespace net = boost::asio;
namespace ssl = net::ssl;

namespace keep_my_journal {
// all functions here are implemented in each exchanges' price_stream source
void binance_price_watcher(net::io_context &, ssl::context &);
void okexchange_price_watcher(net::io_context &, ssl::context &);
void kucoin_price_watcher(net::io_context &, ssl::context &);

#ifdef CRYPTOLOG_USING_MSGPACK
void start_prices_deposit_into_storage(bool &);
#endif

} // namespace keep_my_journal

int main() {
  unsigned int const native_thread_size = std::thread::hardware_concurrency();
  net::io_context ioContext((int)native_thread_size);
  ssl::context sslContext(ssl::context::tlsv12_client);
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

  net::signal_set signalSet(ioContext, SIGTERM);
  signalSet.add(SIGABRT);
  bool running = true;

  signalSet.async_wait(
      [&ioContext, &running](boost::system::error_code const &error,
                             int const signalNumber) {
        if (!ioContext.stopped()) {
          ioContext.stop();
          running = false;
        }
      });

  std::thread binanceWatcher{[&ioContext, &sslContext] {
    keep_my_journal::binance_price_watcher(ioContext, sslContext);
  }};

  std::thread okWatcher{[&ioContext, &sslContext] {
    keep_my_journal::okexchange_price_watcher(ioContext, sslContext);
  }};

  std::thread kucoinWatcher{[&ioContext, &sslContext] {
    keep_my_journal::kucoin_price_watcher(ioContext, sslContext);
  }};

#ifdef CRYPTOLOG_USING_MSGPACK
  std::thread dataTransmitter{[&running] {
    keep_my_journal::start_prices_deposit_into_storage(running);
  }};
#endif

  // wait a bit for all tasks to start up and have async "actions" lined up
  binanceWatcher.join();
  okWatcher.join();
  kucoinWatcher.join();

#ifdef CRYPTOLOG_USING_MSGPACK
  dataTransmitter.join();
#endif

  ioContext.run();

  std::cout << "Done running stuff..." << std::endl;
  return EXIT_SUCCESS;
}
