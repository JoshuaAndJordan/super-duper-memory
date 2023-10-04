// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include <boost/asio/ssl/context.hpp>
#include <cppzmq/zmq.hpp>
#include <filesystem>
#include <msgpack.hpp>
#include <spdlog/spdlog.h>
#include <thread>

#include "account_stream/binance_order_info.hpp"
#include "account_stream/okex_order_info.hpp"
#include "account_stream/user_scheduled_task.hpp"

namespace net = boost::asio;
namespace ssl = net::ssl;

namespace keep_my_journal {

class binance_stream_t;
class kucoin_ua_stream_t;
class okex_stream_t;

using binance_stream_list_t = std::vector<std::shared_ptr<binance_stream_t>>;
using kucoin_stream_list_t = std::vector<std::shared_ptr<kucoin_ua_stream_t>>;
using okex_stream_list_t = std::vector<std::shared_ptr<okex_stream_t>>;

utils::waitable_container_t<account_monitor_task_result_t> monitorStatusResults;

void addBinanceAccountStream(binance_stream_list_t &, account_info_t const &,
                             net::io_context &ioContext,
                             ssl::context &sslContext);
void addOkexAccountStream(okex_stream_list_t &, account_info_t const &,
                          net::io_context &ioContext, ssl::context &sslContext);
void addKucoinAccountStream(kucoin_stream_list_t &, account_info_t const &,
                            trade_type_e, net::io_context &ioContext,
                            ssl::context &sslContext);
void removeKucoinAccountStream(kucoin_stream_list_t &, account_info_t const &);
void removeBinanceAccountStream(binance_stream_list_t &,
                                account_info_t const &);
void removeOkexAccountStream(okex_stream_list_t &, account_info_t const &);

template <typename Stream>
void exchangeResultWatcher(std::string const &pathPrefix,
                           std::string const &exchangeName,
                           zmq::context_t &msgContext, Stream &resultStream,
                           bool &isRunning) {
  auto const address = fmt::format("ipc://{}/{}", pathPrefix, exchangeName);
  zmq::socket_t writerSocket(msgContext, zmq::socket_type::pub);
  writerSocket.bind(address);

  msgpack::sbuffer outBuffer;
  while (isRunning) {
    auto data = resultStream.get();
    std::visit(
        [&outBuffer, &writerSocket](auto &&data) mutable {
          msgpack::pack(outBuffer, data);
          zmq::message_t msg(outBuffer.data(), outBuffer.size());
          writerSocket.send(msg, zmq::send_flags::none);

          outBuffer.clear();
        },
        data);
  }
}

std::optional<account_scheduled_task_t>
get_scheduled_task(zmq::socket_t &socket) {
  zmq::message_t message{};
  if (auto const optRecv = socket.recv(message); !optRecv.has_value()) {
    spdlog::error("unable to receive valid message from socket");
    return std::nullopt;
  }

  auto const oh = msgpack::unpack((char const *)message.data(), message.size());
  auto object = oh.get();

  account_scheduled_task_t task{};
  object.convert(task);
  return task;
}

account_info_t acct_info_from_task(account_scheduled_task_t const &task) {
  account_info_t info;
  info.passphrase = task.passphrase;
  info.secretKey = task.secretKey;
  info.apiKey = task.apiKey;
  info.userID = task.userID;
  return info;
}

void binanceAccountMonitor(std::string const &path, zmq::context_t &msgContext,
                           net::io_context &ioContext, ssl::context &sslContext,
                           bool &isRunning) {
  binance_stream_list_t binanceStreams{};
  zmq::socket_t receiverSocket(msgContext, zmq::socket_type::sub);
  auto const address = fmt::format("ipc://{}/binance", path);
  receiverSocket.connect(address);

  while (isRunning) {
    auto optTask = get_scheduled_task(receiverSocket);
    if (!optTask.has_value())
      continue;

    if (optTask->exchange != exchange_e::binance)
      continue;

    auto const accountInfo = acct_info_from_task(*optTask);

    account_monitor_task_result_t result{};
    result.taskID = optTask->taskID;
    result.userID = optTask->userID;
    result.state = task_state_e::running;

    if (optTask->operation == task_operation_e::add) {
      addBinanceAccountStream(binanceStreams, accountInfo, ioContext,
                              sslContext);
    } else if (optTask->operation == task_operation_e::remove) {
      removeBinanceAccountStream(binanceStreams, accountInfo);
    } else {
      result.state = task_state_e::stopped;
    }

    monitorStatusResults.append(result);
  }
  receiverSocket.close();
}

void kucoinAccountMonitor(std::string const &path, zmq::context_t &msgContext,
                          net::io_context &ioContext, ssl::context &sslContext,
                          bool &isRunning) {
  kucoin_stream_list_t streams{};
  auto const address = fmt::format("ipc://{}/kucoin", path);
  zmq::socket_t receiverSocket(msgContext, zmq::socket_type::sub);
  receiverSocket.connect(address);

  while (isRunning) {
    auto optTask = get_scheduled_task(receiverSocket);
    if (!optTask.has_value())
      continue;

    if (optTask->exchange != exchange_e::kucoin)
      continue;

    auto const accountInfo = acct_info_from_task(*optTask);
    account_monitor_task_result_t result{};
    result.taskID = optTask->taskID;
    result.userID = optTask->userID;
    result.state = task_state_e::running;

    if (optTask->operation == task_operation_e::add) {
      addKucoinAccountStream(streams, accountInfo, optTask->tradeType,
                             ioContext, sslContext);
    } else if (optTask->operation == task_operation_e::remove) {
      removeKucoinAccountStream(streams, accountInfo);
    } else {
      result.state = task_state_e::stopped;
    }

    monitorStatusResults.append(result);
  }
  receiverSocket.close();
}

void okexAccountMonitor(std::string const &path, zmq::context_t &msgContext,
                        net::io_context &ioContext, ssl::context &sslContext,
                        bool &isRunning) {
  okex_stream_list_t streams{};
  zmq::socket_t receiverSocket(msgContext, zmq::socket_type::sub);

  auto const address = fmt::format("ipc://{}/okex", path);
  receiverSocket.connect(address);

  while (isRunning) {
    auto optTask = get_scheduled_task(receiverSocket);
    if (!optTask.has_value())
      continue;

    if (optTask->exchange != exchange_e::okex)
      continue;

    auto const accountInfo = acct_info_from_task(*optTask);
    account_monitor_task_result_t result{};
    result.taskID = optTask->taskID;
    result.userID = optTask->userID;
    result.state = task_state_e::running;

    if (optTask->operation == task_operation_e::add)
      addOkexAccountStream(streams, accountInfo, ioContext, sslContext);
    else if (optTask->operation == task_operation_e::remove)
      removeOkexAccountStream(streams, accountInfo);
    else
      result.state = task_state_e::stopped;
    monitorStatusResults.append(result);
  }
  receiverSocket.close();
}

void binanceResultWatcher(std::string const &path, zmq::context_t &msgContext,
                          bool &isRunning) {
  auto &stream = binance::account_stream_sink_t::get_account_stream();
  exchangeResultWatcher(path, "binance", msgContext, stream, isRunning);
}

void okexResultWatcher(std::string const &path, zmq::context_t &msgContext,
                       bool &isRunning) {
  auto &stream = okex::account_stream_sink_t::get_account_stream();
  exchangeResultWatcher(path, "okex", msgContext, stream, isRunning);
}

void launchResultWriters(zmq::context_t &msgContext, bool &isRunning) {
  auto const path = "/tmp/cryptolog/stream/account/result";
  if (!std::filesystem::exists(path))
    std::filesystem::create_directories(path);

  std::thread binanceResultWriter{
      [&] { binanceResultWatcher(path, msgContext, isRunning); }};

  std::thread okexResultWriter{
      [&] { okexResultWatcher(path, msgContext, isRunning); }};

  binanceResultWriter.join();
  okexResultWriter.join();
}

void start_task_status_writer(zmq::context_t &msgContext, bool &isRunning) {
  auto const path = "/tmp/cryptolog/stream/account";
  if (!std::filesystem::exists(path))
    std::filesystem::create_directories(path);

  zmq::socket_t senderSocket(msgContext, zmq::socket_type::pub);
  senderSocket.bind(fmt::format("ipc://{}/monitor", path));

  msgpack::sbuffer buffer;
  while (isRunning) {
    auto result = monitorStatusResults.get();
    msgpack::pack(buffer, result);
    std::string_view const view(buffer.data(), buffer.size());
    zmq::message_t message(view);
    senderSocket.send(message, zmq::send_flags::none);
    buffer.clear();
  }
  senderSocket.close();
}

void externalAccountMessageMonitor(net::io_context &ioContext,
                                   ssl::context &sslContext, bool &isRunning) {
  auto const path = "/tmp/cryptolog/stream/account/task";
  if (!std::filesystem::exists(path))
    std::filesystem::create_directories(path);

  zmq::context_t context{(int)std::thread::hardware_concurrency()};

  std::thread{[&isRunning, &context] {
    launchResultWriters(context, isRunning);
  }}.detach();
  std::thread binanceThread{[&, path] {
    spdlog::info("Launching binance account monitor...");
    binanceAccountMonitor(path, context, ioContext, sslContext, isRunning);
  }};

  std::thread kucoinThread{[&, path] {
    spdlog::info("Launching kucoin account monitor...");
    kucoinAccountMonitor(path, context, ioContext, sslContext, isRunning);
  }};

  std::thread okexThread{[&, path] {
    spdlog::info("Launching OKEX account monitor...");
    okexAccountMonitor(path, context, ioContext, sslContext, isRunning);
  }};

  start_task_status_writer(context, isRunning);

  binanceThread.join();
  kucoinThread.join();
  okexThread.join();
}

} // namespace keep_my_journal
