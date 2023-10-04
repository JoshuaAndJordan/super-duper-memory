// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include <filesystem>
#include <thread>

#include <cppzmq/zmq.hpp>
#include <price_stream/commodity.hpp>
#include <spdlog/spdlog.h>

namespace keep_my_journal {
namespace utils {
std::string exchangesToString(exchange_e);
}

void exchangesPriceWatcher(zmq::context_t &msgContext, bool &isRunning,
                           std::string const &path, exchange_e const exchange) {
  auto const filename = utils::exchangesToString(exchange);
  auto const address = fmt::format("ipc://{}/{}", path, filename);
  spdlog::info("The exchange price address is {}", address);

  zmq::socket_t receivingSocket{msgContext, zmq::socket_type::sub};
  try {
    receivingSocket.connect(address);
  } catch (zmq::error_t const &e) {
    spdlog::error("Error connecting to {}: {}", address, e.what());
    return;
  }

  auto &instruments = instrument_sink_t::get_unique_instruments(exchange);

  while (isRunning) {
    zmq::message_t message;
    auto const optSize = receivingSocket.recv(message, zmq::recv_flags::none);
    if (!optSize.has_value()) {
      spdlog::error("There was an error receiving this message...");
      continue;
    }

    auto const view = message.to_string_view();
    auto const unpackedMsg = msgpack::unpack(view.data(), view.size());
    auto const object = unpackedMsg.get();

    instrument_type_t instrument;
    try {
      object.convert(instrument);
    } catch (msgpack::type_error const &e) {
      spdlog::error(e.what());
      continue;
    }

    instruments.insert(std::move(instrument));
  }

  spdlog::info("Closing socket for {}", filename);
  receivingSocket.close();
}

void launch_price_watchers(bool &isRunning) {
  static auto const path = "/tmp/cryptolog/stream/price";
  if (!std::filesystem::exists(path))
    std::filesystem::create_directories(path);

  int const threadCount = (int)std::thread::hardware_concurrency();
  zmq::context_t msgContext{threadCount};

  std::thread binancePrices{[&msgContext, &isRunning] {
    exchangesPriceWatcher(msgContext, isRunning, path, exchange_e::binance);
  }};

  std::thread kucoinPrices{[&msgContext, &isRunning] {
    exchangesPriceWatcher(msgContext, isRunning, path, exchange_e::kucoin);
  }};

  std::thread okexPrices{[&msgContext, &isRunning] {
    exchangesPriceWatcher(msgContext, isRunning, path, exchange_e::okex);
  }};
  binancePrices.join();
  kucoinPrices.join();
  okexPrices.join();
}
} // namespace keep_my_journal
