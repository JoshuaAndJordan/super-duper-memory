// Copyright (C) 2023 Joshua and Jordan Ogunyinka

// monitor all account activities, buying selling deposit withdrawal (all
// read-only)
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/context.hpp>

#include <iostream>
#include <thread>

namespace net = boost::asio;
namespace ssl = net::ssl;

#ifdef CRYPTOLOG_USING_MSGPACK
namespace keep_my_journal {
void externalAccountMessageMonitor(net::io_context &, ssl::context &,
                                   bool &isRunning);
}
#endif

int main() {
  net::io_context ioContext(std::thread::hardware_concurrency());
  ssl::context sslContext(ssl::context::tlsv12_client);
  bool isRunning = true;

  sslContext.set_default_verify_paths();
  sslContext.set_verify_mode(ssl::verify_none);

  net::signal_set signalSet(ioContext, SIGTERM);
  signalSet.add(SIGABRT);

  signalSet.async_wait(
      [&ioContext, &isRunning](boost::system::error_code const &error,
                               int const signalNumber) {
        if (!ioContext.stopped()) {
          ioContext.stop();
          isRunning = false;
        }
      });

#ifdef CRYPTOLOG_USING_MSGPACK
  std::thread externalMonitorThread{[&] {
    keep_my_journal::externalAccountMessageMonitor(ioContext, sslContext,
                                                   isRunning);
  }};
  externalMonitorThread.join();
#endif

  ioContext.run();
  return EXIT_SUCCESS;
}
