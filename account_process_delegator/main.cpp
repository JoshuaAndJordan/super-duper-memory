#include <CLI/CLI11.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/context.hpp>
#include <thread>

#include "file_utils.hpp"
#include "server.hpp"

namespace net = boost::asio;

namespace keep_my_journal {
void monitor_tokens_latest_prices(bool &isRunning);
void account_stream_scheduled_task_writer(bool &isRunning);
void price_result_list_watcher(bool &isRunning);
} // namespace keep_my_journal

std::string BEARER_TOKEN_SECRET_KEY;

int main(int argc, char *argv[]) {
  CLI::App cli_parser{
      "an asynchronous web server for monitoring crypto price_stream "
      "and user information"};
  keep_my_journal::command_line_interface_t args{};

  cli_parser.add_option("-p", args.port, "port to bind server to");
  cli_parser.add_option("-a", args.ip_address, "IP address to use");
  CLI11_PARSE(cli_parser, argc, argv)

  auto &ioContext = keep_my_journal::get_io_context();
  auto server_instance =
      std::make_shared<keep_my_journal::server_t>(ioContext, std::move(args));
  if (!server_instance->run())
    return EXIT_FAILURE;

  boost::asio::ssl::context sslContext(
      boost::asio::ssl::context::tlsv12_client);
  sslContext.set_default_verify_paths();
  sslContext.set_verify_mode(boost::asio::ssl::verify_none);

  bool isRunning = true;

  {
    // connect to the price watching process and get the latest prices from the
    // price_stream
    std::thread{[&isRunning] {
      // defined in latest_prices_watcher.cpp
      keep_my_journal::monitor_tokens_latest_prices(isRunning);
    }}.detach();

    // launch sockets that writes monitoring data to wire
    std::thread{[&isRunning] {
      // account activities include balance change, orders, trades etc
      // defined in scheduled_account_tasks.cpp
      keep_my_journal::account_stream_scheduled_task_writer(isRunning);
    }}.detach();

    // monitors the percentage/time-based price task results
    std::thread{[&isRunning] {
      // defined in scheduled_price_tasks.cpp
      keep_my_journal::price_result_list_watcher(isRunning);
    }}.detach();
  }

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

  auto const thread_count = std::thread::hardware_concurrency();
  auto const reserved_thread_count = thread_count > 2 ? thread_count - 2 : 1;
  std::vector<std::thread> threads{};
  threads.reserve(reserved_thread_count);
  for (std::size_t counter = 0; counter < reserved_thread_count; ++counter) {
    threads.emplace_back([&] { ioContext.run(); });
  }

  ioContext.run();
  return EXIT_SUCCESS;
}
