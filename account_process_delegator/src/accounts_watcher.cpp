// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include <filesystem>
#include <thread>

#include "account_stream/user_scheduled_task.hpp"
#include <cppzmq/zmq.hpp>
#include <price_stream/commodity.hpp>
#include <spdlog/spdlog.h>

namespace keep_my_journal {
namespace utils {
std::string exchangesToString(exchange_e);
}

utils::waitable_container_t<account_scheduled_task_t> taskMonitorQueue{};
std::deque<account_monitor_task_result_t> monitoredTaskResults{};

std::optional<account_monitor_task_result_t>
queue_monitoring_task(account_scheduled_task_t const &task) {
  static auto const max_time_limit = std::chrono::seconds(20);
  taskMonitorQueue.append(task);
  auto iter = monitoredTaskResults.end();
  std::chrono::milliseconds now{0};

  do {
    iter = std::find_if(
        monitoredTaskResults.begin(), monitoredTaskResults.end(),
        [taskID = task.taskID](account_monitor_task_result_t const &result) {
          return result.taskID == taskID;
        });
    if (iter != monitoredTaskResults.end())
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    now += std::chrono::milliseconds(100);
  } while (iter == monitoredTaskResults.end() && (now < max_time_limit));

  if (iter != monitoredTaskResults.end()) {
    auto const result = *iter;
    monitoredTaskResults.erase(iter);
    return result;
  }
  return std::nullopt;
}

void write_data_to_wire(zmq::context_t &msgContext, msgpack::sbuffer &buffer,
                        account_scheduled_task_t const &task,
                        std::string const &path) {
  auto const address =
      fmt::format("ipc://{}/{}", path, utils::exchangesToString(task.exchange));
  zmq::socket_t sendingSocket(msgContext, zmq::socket_type::pub);
  sendingSocket.bind(address);

  msgpack::pack(buffer, task);
  std::string_view const v(buffer.data(), buffer.size());

  zmq::message_t message(v);

  auto const optSize = sendingSocket.send(message, zmq::send_flags::none);
  if (!optSize.has_value())
    throw std::runtime_error("unable to send message on address " + address);
}

void monitor_scheduled_tasks_result(bool &isRunning,
                                    zmq::context_t &msgContext) {
  static size_t const resultBufferLimit = 5'000;

  zmq::socket_t recvSocket(msgContext, zmq::socket_type::sub);
  recvSocket.connect("ipc:///tmp/cryptolog/stream/account/monitor");
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

void launch_account_monitor_sender(bool &isRunning) {
  auto const path = "/tmp/cryptolog/stream/account/task";
  if (!std::filesystem::exists(path))
    std::filesystem::create_directories(path);

  zmq::context_t msgContext{};
  msgpack::sbuffer buffer;

  std::thread monitorResultThread{[&msgContext, &isRunning] {
    monitor_scheduled_tasks_result(isRunning, msgContext);
  }};

  while (isRunning) {
    auto task = taskMonitorQueue.get();

    try {
      write_data_to_wire(msgContext, buffer, task, path);
    } catch (std::exception const &e) {
      spdlog::error(e.what());
    }

    buffer.clear();
  }
}
} // namespace keep_my_journal