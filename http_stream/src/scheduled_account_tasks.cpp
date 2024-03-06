// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include <filesystem>
#include <thread>

#include "account_stream/user_scheduled_task.hpp"
#include "macro_defines.hpp"
#include <cppzmq/zmq.hpp>
#include <price_stream/commodity.hpp>
#include <spdlog/spdlog.h>

namespace keep_my_journal {
namespace utils {
std::string exchangesToString(exchange_e);
bool validate_address_paradigm(char const *address);
} // namespace utils

utils::waitable_container_t<account_scheduled_task_t> taskMonitorQueue{};
std::deque<account_monitor_task_result_t> monitoredTaskResults{};

std::optional<account_monitor_task_result_t>
queue_account_stream_tasks(account_scheduled_task_t const &task) {
  static auto const max_time_limit = std::chrono::seconds(20);
  static auto const hundred_ms = std::chrono::milliseconds(100);
  taskMonitorQueue.append(task);

  std::deque<account_monitor_task_result_t>::iterator iter;
  std::chrono::milliseconds now{0};

  do {
    iter = std::find_if(
        monitoredTaskResults.begin(), monitoredTaskResults.end(),
        [taskID = task.taskID](account_monitor_task_result_t const &result) {
          return result.taskID == taskID;
        });
    if (iter != monitoredTaskResults.end())
      break;
    std::this_thread::sleep_for(hundred_ms);
    now += hundred_ms;
  } while (iter == monitoredTaskResults.end() && (now < max_time_limit));

  if (iter != monitoredTaskResults.end()) {
    auto result = *iter;
    monitoredTaskResults.erase(iter);
    return result;
  }
  return std::nullopt;
}

void write_scheduled_task_to_stream(
    std::shared_ptr<zmq::socket_t> &sendingSocket, msgpack::sbuffer &buffer,
    account_scheduled_task_t const &task) {

  msgpack::pack(buffer, task);
  std::string_view const v(buffer.data(), buffer.size());

  zmq::message_t message(v);
  auto const optSize = sendingSocket->send(message, zmq::send_flags::none);
  if (!optSize.has_value()) {
    throw std::runtime_error("unable to send message on address " +
                             utils::exchangesToString(task.exchange));
  }
}

void monitor_scheduled_tasks_result(bool &isRunning,
                                    zmq::context_t &msgContext) {
  static size_t const resultBufferLimit = 5'000;

  zmq::socket_t recvSocket(msgContext, zmq::socket_type::sub);
  recvSocket.set(zmq::sockopt::subscribe, "");

  auto const address = fmt::format(
      "ipc://{}/writer", SCHEDULED_ACCOUNT_TASK_IMMEDIATE_RESULT_PATH);
  spdlog::info("{} sub {}", __func__, address);

  recvSocket.connect(address);

  while (isRunning) {
    zmq::message_t message{};
    if (auto const optRecv = recvSocket.recv(message); !optRecv.has_value()) {
      spdlog::error("unable to receive valid message from socket");
      continue;
    }

    auto const oh =
        msgpack::unpack((char const *)message.data(), message.size());
    auto object = oh.get();

    account_monitor_task_result_t result{};
    object.convert(result);
    if (monitoredTaskResults.size() == resultBufferLimit)
      monitoredTaskResults.pop_front();
    monitoredTaskResults.emplace_back(result);
  }
}

void account_stream_scheduled_task_writer(bool &isRunning) {
  if (!utils::validate_address_paradigm(EXCHANGE_STREAM_TASK_SCHEDULER_PATH))
    return;

  zmq::context_t msgContext{};
  msgpack::sbuffer buffer;

  std::thread monitorResultThread{[&msgContext, &isRunning] {
    monitor_scheduled_tasks_result(isRunning, msgContext);
  }};

  std::map<exchange_e, std::shared_ptr<zmq::socket_t>> sockets;
  for (auto const exchange :
       {exchange_e::binance, exchange_e::kucoin, exchange_e::okex}) {
    auto const address =
        fmt::format("ipc://{}/{}", EXCHANGE_STREAM_TASK_SCHEDULER_PATH,
                    utils::exchangesToString(exchange));
    spdlog::info("Address -> {}", address);
    auto socket =
        std::make_shared<zmq::socket_t>(msgContext, zmq::socket_type::pub);
    socket->bind(address);
    sockets[exchange] = socket;
  }

  while (isRunning) {
    auto task = taskMonitorQueue.get();

    try {
      write_scheduled_task_to_stream(sockets[task.exchange], buffer, task);
    } catch (std::exception const &e) {
      spdlog::error(e.what());
    }

    buffer.clear();
  }

  for (auto &[_, socket] : sockets) {
    // socket.unbind(address);
    socket->close();
    socket.reset();
  }
}
} // namespace keep_my_journal