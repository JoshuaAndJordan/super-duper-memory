// Copyright (C) 2023 Joshua & Jordan Ogunyinka

#include <cppzmq/zmq.hpp>
#include <filesystem>
#include <msgpack.hpp>
#include <thread>

#include "price_stream/commodity.hpp"
#include "spdlog/spdlog.h"
#include "string_utils.hpp"

namespace keep_my_journal {

void data_transmission(zmq::context_t &context, bool &running,
                       std::string const &path, exchange_e const exchange) {
  auto const filename = utils::exchangesToString(exchange);
  auto const address = fmt::format("ipc://{}/{}", path, filename);
  spdlog::info("The address is {}", address);
  auto &instruments = instrument_sink_t::get_all_listed_instruments(exchange);

  zmq::socket_t senderSocket{context, zmq::socket_type::pub};
  try {
    senderSocket.bind(address);
  } catch (zmq::error_t const &e) {
    spdlog::error(e.what());
    throw;
  }

  msgpack::sbuffer serialBuffer;

  while (running) {
    auto instrument = instruments.get();
    msgpack::pack(serialBuffer, instrument);

    std::string_view view(serialBuffer.data(), serialBuffer.size());
    zmq::message_t message(view);
    if (auto const optSize = senderSocket.send(message, zmq::send_flags::none);
        !optSize.has_value()) {
      spdlog::error("Unable to send message...");
    }
    serialBuffer.clear();
  }

  spdlog::info("Closing/unbinding socket...");
  senderSocket.close();
}

void start_data_transmission(bool &running) {
  int const threadCount = (int)std::thread::hardware_concurrency();
  zmq::context_t context{threadCount};

  static auto const path = "/tmp/cryptolog/stream/price";
  if (!std::filesystem::exists(path))
    std::filesystem::create_directories(path);

  std::thread binanceDataSender{[&context, &running] {
    data_transmission(context, running, path, exchange_e::binance);
  }};

  std::thread kucoinDataSender{[&context, &running] {
    data_transmission(context, running, path, exchange_e::kucoin);
  }};

  std::thread okDataSender{[&context, &running] {
    data_transmission(context, running, path, exchange_e::okex);
  }};

  binanceDataSender.join();
  kucoinDataSender.join();
  okDataSender.join();
}
} // namespace keep_my_journal
