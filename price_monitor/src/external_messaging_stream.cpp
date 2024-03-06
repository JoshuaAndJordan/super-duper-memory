// Copyright (C) 2023 Joshua & Jordan Ogunyinka

#include <cppzmq/zmq.hpp>
#include <filesystem>
#include <msgpack.hpp>
#include <thread>

#include "macro_defines.hpp"
#include "price_stream/commodity.hpp"
#include "spdlog/spdlog.h"
#include "string_utils.hpp"

namespace keep_my_journal {
namespace utils {
bool validate_address_paradigm(char const *address);
}

void store_exchanges_price_into_storage(zmq::context_t &context, bool &running,
                                        exchange_e const exchange) {
  auto const filename = utils::exchangesToString(exchange);
  auto const address =
      fmt::format("ipc://{}/{}", PRICE_MONITOR_STREAM_DEPOSIT_PATH, filename);
  spdlog::info("The address is {}", address);
  auto &instruments = instrument_sink_t::get_all_listed_instruments(exchange);

  zmq::socket_t senderSocket{context, zmq::socket_type::xpub};
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
    auto const optSize = senderSocket.send(message, zmq::send_flags::none);
    if (!optSize.has_value()) {
      spdlog::error("Unable to send message...");
      continue;
    }

    serialBuffer.clear();
  }

  spdlog::info("Closing/unbinding socket...");
  senderSocket.close();
}

void start_prices_deposit_into_storage(bool &running) {
  if (!utils::validate_address_paradigm(PRICE_MONITOR_STREAM_DEPOSIT_PATH))
    return;

  int const threadCount = (int)std::thread::hardware_concurrency();
  zmq::context_t context{threadCount};

  std::thread binanceDataSender{[&context, &running] {
    store_exchanges_price_into_storage(context, running, exchange_e::binance);
  }};

  std::thread kucoinDataSender{[&context, &running] {
    store_exchanges_price_into_storage(context, running, exchange_e::kucoin);
  }};

  std::thread okDataSender{[&context, &running] {
    store_exchanges_price_into_storage(context, running, exchange_e::okex);
  }};

  binanceDataSender.join();
  kucoinDataSender.join();
  okDataSender.join();
}
} // namespace keep_my_journal
