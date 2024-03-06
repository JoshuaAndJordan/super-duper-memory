#include <cppzmq/zmq.hpp>
#include <filesystem>
#include <price_stream/commodity.hpp>
#include <spdlog/spdlog.h>
#include <thread>

#include "macro_defines.hpp"

using keep_my_journal::instrument_exchange_set_t;
extern instrument_exchange_set_t uniqueInstruments;

namespace keep_my_journal {
namespace utils {
std::string exchangesToString(exchange_e);
std::string tradeTypeToString(trade_type_e tradeType);
bool validate_address_paradigm(char const *address);
} // namespace utils

template <exchange_e exchange>
void exchangesPriceWatcher(zmq::context_t &msgContext, bool &isRunning) {
  auto const filename = utils::exchangesToString(exchange);
  auto const address =
      fmt::format("ipc://{}/{}", PRICE_MONITOR_STREAM_DEPOSIT_PATH, filename);

  zmq::socket_t receivingSocket{msgContext, zmq::socket_type::sub};
  receivingSocket.set(zmq::sockopt::subscribe, "");

  try {
    receivingSocket.connect(address);
  } catch (zmq::error_t const &e) {
    return spdlog::error("Error connecting to {}: {}", address, e.what());
  }

  auto &instruments = uniqueInstruments[exchange];
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
    instrument_type_t instrument{};

    try {
      object.convert(instrument);
    } catch (msgpack::type_error const &e) {
      spdlog::error(e.what());
      continue;
    }
    instruments.insert(instrument);
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
    exchangesPriceWatcher<exchange_e::binance>(msgContext, isRunning);
  }};

  std::thread kucoinPrices{[&msgContext, &isRunning] {
    exchangesPriceWatcher<exchange_e::kucoin>(msgContext, isRunning);
  }};

  std::thread okexPrices{[&msgContext, &isRunning] {
    exchangesPriceWatcher<exchange_e::okex>(msgContext, isRunning);
  }};

  binancePrices.join();
  kucoinPrices.join();
  okexPrices.join();
}

} // namespace keep_my_journal