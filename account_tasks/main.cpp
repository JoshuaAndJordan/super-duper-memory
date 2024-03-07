// Copyright (C) 2023-2024 Joshua and Jordan Ogunyinka

#include <cppzmq/zmq.hpp>
#include <msgpack/adaptor/cpp17/variant.hpp>
#include <spdlog/spdlog.h>

#include "account_stream/binance_order_info.hpp"
#include "account_stream/okex_order_info.hpp"
#include "container.hpp"
#include "http_rest_client.hpp"
#include "json_utils.hpp"
#include "macro_defines.hpp"

namespace keep_my_journal {
namespace utils {
#ifdef CRYPTOLOG_USING_MSGPACK
std::string exchangesToString(exchange_e exchange);
#endif
} // namespace utils

template <typename T>
[[noreturn]] void http_send_result(net::io_context &ioContext,
                                   utils::waitable_container_t<T> &container,
                                   exchange_e const exchange) {
  auto const path =
      fmt::format("/account_result/{}", utils::exchangesToString(exchange));
  static std::unique_ptr<http_rest_client_t> client =
      std::make_unique<http_rest_client_t>(ioContext, "localhost", "14577",
                                           path);
  while (true) {
    auto data = container.get();
    std::visit(
        [](auto &&data) {
          auto const &payload = json(data).dump();
          client->add_payload(payload);
          client->send_data();
        },
        data);
  }
}

template <typename AccountMsgType>
[[noreturn]] void monitor_account_data_stream(net::io_context &ioContext,
                                              zmq::context_t &messageContext,
                                              exchange_e const exchange) {
  zmq::socket_t receivingSocket(messageContext, zmq::socket_type::sub);
  receivingSocket.set(zmq::sockopt::subscribe, "");

  auto const receivingAddress =
      fmt::format("ipc://{}/{}", EXCHANGE_STREAM_RESULT_DEPOSIT_PATH,
                  utils::exchangesToString(exchange));
  receivingSocket.connect(receivingAddress);
  utils::waitable_container_t<AccountMsgType> resultContainer{};
  std::thread{[&resultContainer, &ioContext, exchange] {
    http_send_result(ioContext, resultContainer, exchange);
  }}.detach();

  while (true) {
    zmq::message_t message{};
    if (auto const optRecv = receivingSocket.recv(message);
        !optRecv.has_value()) {
      spdlog::error("unable to receive valid message from socket");
      continue;
    }

    auto oh = msgpack::unpack((char const *)message.data(), message.size());
    AccountMsgType accountMsgType{oh.get().as<AccountMsgType>()};
    resultContainer.append(std::move(accountMsgType));
  }
}
} // namespace keep_my_journal

using keep_my_journal::net::io_context;

int main() {
  std::this_thread::sleep_for(std::chrono::seconds(5));
  io_context ioContext((int)std::thread::hardware_concurrency());
  zmq::context_t messageContext{};

  // binance
  std::thread binanceThread{[&messageContext, &ioContext] {
    keep_my_journal::monitor_account_data_stream<
        keep_my_journal::binance::stream_data_t>(
        ioContext, messageContext, keep_my_journal::exchange_e::binance);
  }};

  // OK Exchange
  std::thread okexThread{[&messageContext, &ioContext] {
    keep_my_journal::monitor_account_data_stream<
        keep_my_journal::okex::okex_ws_data_t>(
        ioContext, messageContext, keep_my_journal::exchange_e::okex);
  }};

  binanceThread.join();
  okexThread.join();
  return EXIT_SUCCESS;
}
