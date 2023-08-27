#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

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
      boost::asio::ssl::context::sslv23_client);
  sslContext.set_default_verify_paths();
  sslContext.set_verify_mode(boost::asio::ssl::verify_none);

  // std::thread binanceWatcher{[&ioContext, &sslContext] {
  // jordan::binance_price_watcher(ioContext, sslContext);
  // }};

  // std::thread kucoinWatcher{[&ioContext, &sslContext] {
  jordan::kucoin_price_watcher(ioContext, sslContext);
  // }};

  // std::thread okWatcher{[&ioContext, &sslContext] {
  jordan::okexchange_price_watcher(ioContext, sslContext);
  //}};

  std::vector<std::thread> threads{};
  threads.reserve(native_thread_size);
  for (int counter = 0; counter < native_thread_size; ++counter)
    threads.emplace_back([&ioContext] { ioContext.run(); });

  // wait a bit for all tasks to start up and have async "actions" lined up
  // binanceWatcher.join();
  // kucoinWatcher.join();
  // okWatcher.join();

  ioContext.run();

  std::cout << "Done running stuff..." << std::endl;
  return EXIT_SUCCESS;
}
