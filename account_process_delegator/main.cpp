#include <CLI/CLI11.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/context.hpp>
#include <thread>

#include "database_connector.hpp"
#include "file_utils.hpp"
#include "server.hpp"

namespace net = boost::asio;

namespace keep_my_journal {
void monitor_tokens_latest_prices(bool &isRunning);
void account_stream_scheduled_task_writer(bool &isRunning);
net::io_context &get_io_context();
} // namespace keep_my_journal

std::string BEARER_TOKEN_SECRET_KEY;

int main(int argc, char *argv[]) {
  CLI::App cli_parser{
      "an asynchronous web server for monitoring crypto price_stream "
      "and user information"};
  keep_my_journal::command_line_interface_t args{};

  cli_parser.add_option("-p", args.port, "port to bind server to");
  cli_parser.add_option("-a", args.ip_address, "IP address to use");
  cli_parser.add_option("-d", args.database_config_filename,
                        "Database config filename");
  cli_parser.add_option("-y", args.launch_type,
                        "Launch type(production, development)");
  CLI11_PARSE(cli_parser, argc, argv)

  auto const software_config = keep_my_journal::utils::parseConfigFile(
      args.database_config_filename, args.launch_type);
  if (!software_config) {
    std::cerr << "Unable to get database configuration values\n";
    return EXIT_FAILURE;
  }

  auto &database_connector =
      keep_my_journal::database_connector_t::s_get_db_connector();
  database_connector->set_username(software_config->dbUsername);
  database_connector->set_password(software_config->dbPassword);
  database_connector->set_database_name(software_config->dbDns);

  if (!database_connector->connect())
    return EXIT_FAILURE;

  BEARER_TOKEN_SECRET_KEY = software_config->jwtSecretKey;

  auto &ioContext = keep_my_journal::get_io_context();
  auto server_instance =
      std::make_shared<keep_my_journal::server_t>(ioContext, std::move(args));
  if (!(*server_instance))
    return EXIT_FAILURE;
  server_instance->run();

  boost::asio::ssl::context sslContext(
      boost::asio::ssl::context::tlsv12_client);
  sslContext.set_default_verify_paths();
  sslContext.set_verify_mode(boost::asio::ssl::verify_none);

  bool isRunning = true;

  {
    // connect to the price watching process and get the latest prices from the
    // price_stream
    std::thread{[&isRunning] {
      keep_my_journal::monitor_tokens_latest_prices(isRunning);
    }}.detach();

    // launch sockets that writes monitoring data to wire
    std::thread{[&isRunning] {
      // account activities include balance change, orders, trades etc
      keep_my_journal::account_stream_scheduled_task_writer(isRunning);
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
