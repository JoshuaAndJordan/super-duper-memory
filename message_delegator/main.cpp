#include <cppzmq/zmq.hpp>
#include <spdlog/spdlog.h>

#include "http_rest_client.hpp"
#include "json_utils.hpp"
#include "macro_defines.hpp"
#include "price_stream/tasks.hpp"

namespace keep_my_journal {
inline std::string
task_result_to_string(scheduled_price_task_result_t const &res) {
  auto str = json(res).dump();
  spdlog::info("Data obtained: {}", str);
  return str;
}

void http_send_result(net::io_context &ioContext,
                      scheduled_price_task_result_t const &result) {
  static std::unique_ptr<http_rest_client_t> client =
      std::make_unique<http_rest_client_t>(ioContext, "localhost", "14576",
                                           "/price_result");
  client->add_payload(task_result_to_string(result));
  client->send_data();
}

void monitor_price_result_stream() {
  zmq::context_t messageContext;
  zmq::socket_t receivingSocket(messageContext, zmq::socket_type::sub);
  receivingSocket.set(zmq::sockopt::subscribe, "");

  auto const receivingAddress =
      fmt::format("ipc://{}", PRICE_MONITOR_TASK_RESULT_PATH);
  receivingSocket.connect(receivingAddress);
  net::io_context ioContext((int)std::thread::hardware_concurrency());

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
    http_send_result(ioContext, result);
  }
}
} // namespace keep_my_journal

int main() {
  keep_my_journal::monitor_price_result_stream();
  return EXIT_SUCCESS;
}
