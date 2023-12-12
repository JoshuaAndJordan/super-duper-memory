// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include <cppzmq/zmq.hpp>
#include <filesystem>
#include <price_stream/commodity.hpp>
#include <spdlog/spdlog.h>
#include <thread>

#include "macro_defines.hpp"

namespace keep_my_journal {
namespace utils {
std::string exchangesToString(exchange_e);
std::string tradeTypeToString(trade_type_e tradeType);

} // namespace utils

void exchangesPriceWatcher(zmq::context_t &msgContext, bool &isRunning,
                           exchange_e const exchange) {
  auto const filename = utils::exchangesToString(exchange);
  auto const address =
      fmt::format("ipc://{}/{}", PRICE_MONITOR_STREAM_DEPOSIT_PATH, filename);

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

    spdlog::info("New price alert: {} -> {} -> {}", instrument.name,
                 utils::tradeTypeToString(instrument.tradeType),
                 instrument.currentPrice);
    instruments.insert(std::move(instrument));
  }

  spdlog::info("Closing socket for {}", filename);
  receivingSocket.close();
}

void monitor_tokens_latest_prices(bool &isRunning) {
  if (!std::filesystem::exists(PRICE_MONITOR_STREAM_DEPOSIT_PATH))
    std::filesystem::create_directories(PRICE_MONITOR_STREAM_DEPOSIT_PATH);

  int const threadCount = (int)std::thread::hardware_concurrency();
  zmq::context_t msgContext{threadCount};

  std::thread binancePrices{[&msgContext, &isRunning] {
    exchangesPriceWatcher(msgContext, isRunning, exchange_e::binance);
  }};

  std::thread kucoinPrices{[&msgContext, &isRunning] {
    exchangesPriceWatcher(msgContext, isRunning, exchange_e::kucoin);
  }};

  std::thread okexPrices{[&msgContext, &isRunning] {
    exchangesPriceWatcher(msgContext, isRunning, exchange_e::okex);
  }};
  binancePrices.join();
  kucoinPrices.join();
  okexPrices.join();
}
} // namespace keep_my_journal
