// monitor all account activities, buying selling deposit withdrawal (all
// read-only)
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <iostream>
#include <thread>

namespace net = boost::asio;
namespace ssl = net::ssl;

#ifdef CRYPTOLOG_USING_MSGPACK
namespace jordan {
void externalAccountMessageMonitor(
    net::io_context &, ssl::context &, bool &isRunning);
}
#endif

int main() {
  net::io_context ioContext(std::thread::hardware_concurrency());
  ssl::context sslContext(ssl::context::tlsv12_client);
  sslContext.set_default_verify_paths();
  sslContext.set_verify_mode(ssl::verify_none);

#ifdef CRYPTOLOG_USING_MSGPACK
  bool isRunning = true;
  std::thread externalMonitorThread {
    [&] {
      jordan::externalAccountMessageMonitor(ioContext, sslContext, isRunning);
    }
  };
  externalMonitorThread.join();
#endif

  ioContext.run();
  return EXIT_SUCCESS;
}
