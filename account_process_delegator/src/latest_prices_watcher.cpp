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
bool validate_address_paradigm(char const *address);
} // namespace utils

void exchangesPriceWatcher(zmq::context_t &msgContext, bool &isRunning,
                           exchange_e const exchange) {
  auto const filename = utils::exchangesToString(exchange);
  auto const address =
      fmt::format("ipc://{}/{}", PRICE_MONITOR_STREAM_DEPOSIT_PATH, filename);

  spdlog::info("In {}, and the address to use is {}", __func__, address);

  zmq::socket_t receivingSocket{msgContext, zmq::socket_type::sub};
  receivingSocket.set(zmq::sockopt::subscribe, "");

  try {
    receivingSocket.connect(address);
  } catch (zmq::error_t const &e) {
    return spdlog::error("Error connecting to {}: {}", address, e.what());
  }

  auto &instruments = instrument_sink_t::get_unique_instruments(exchange);
  while (isRunning) {
    zmq::message_t message;

    if (auto const optSize =
            receivingSocket.recv(message, zmq::recv_flags::none);
        !optSize.has_value()) {
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

void monitor_tokens_latest_prices(bool &isRunning) {
  constexpr int const totalExchanges = static_cast<int>(exchange_e::total);
  zmq::context_t msgContext{totalExchanges};
  if (!utils::validate_address_paradigm(PRICE_MONITOR_STREAM_DEPOSIT_PATH))
    return;

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
