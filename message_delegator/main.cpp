#include <cppzmq/zmq.hpp>
#include <spdlog/spdlog.h>

#include "container.hpp"
#include "http_rest_client.hpp"
#include "json_utils.hpp"
#include "macro_defines.hpp"
#include "price_stream/tasks.hpp"

namespace keep_my_journal {

utils::waitable_container_t<scheduled_price_task_result_t> results;

[[noreturn]] void http_send_result(net::io_context &ioContext) {
  static std::unique_ptr<http_rest_client_t> client =
      std::make_unique<http_rest_client_t>(ioContext, "localhost", "14576",
                                           "/price_result");
  while (true) {
    auto data = results.get();
    auto const &payload = json(data).dump();
    spdlog::info(payload);
    client->add_payload(payload);
    client->send_data();
  }
}

[[noreturn]] void monitor_price_result_stream() {
  zmq::context_t messageContext;
  zmq::socket_t receivingSocket(messageContext, zmq::socket_type::sub);
  receivingSocket.set(zmq::sockopt::subscribe, "");

  auto const receivingAddress =
      fmt::format("ipc://{}", PRICE_MONITOR_TASK_RESULT_PATH);
  receivingSocket.connect(receivingAddress);

  while (true) {
    zmq::message_t message{};
    if (auto const optRecv = receivingSocket.recv(message);
        !optRecv.has_value()) {
      spdlog::error("unable to receive valid message from socket");
      continue;
    }

    auto const oh =
        msgpack::unpack((char const *)message.data(), message.size());
    auto object = oh.get();

    scheduled_price_task_result_t result{};
    object.convert(result);
    results.append(std::move(result));
  }
}
} // namespace keep_my_journal

using keep_my_journal::net::io_context;

int main() {
  std::this_thread::sleep_for(std::chrono::seconds(10));
  io_context ioContext((int)std::thread::hardware_concurrency());

  std::thread(keep_my_journal::monitor_price_result_stream).detach();
  std::thread([&ioContext] {
    keep_my_journal::http_send_result(ioContext);
  }).join();
  return EXIT_SUCCESS;
}
