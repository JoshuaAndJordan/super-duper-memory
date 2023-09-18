// Copyright (C) 2023 Joshua & Jordan Ogunyinka

#include <cppzmq/zmq.hpp>
#include <thread>
#include <msgpack.hpp>
#include <filesystem>
#include "commodity.hpp"
#include "spdlog/spdlog.h"

namespace jordan {

  void data_transmission(zmq::context_t& context, bool &running,
                         exchange_e const exchange,
                         std::string const &filename)
  {
    static auto const path = "/tmp/cryptolog/stream/price";
    if (!std::filesystem::exists(path))
      std::filesystem::create_directories(path);

    auto const address =
        fmt::format("ipc://{}/{}", path, filename);
    spdlog::info("The address is {}", address);
    auto& instruments =
        instrument_sink_t::get_all_listed_instruments(exchange);

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

    std::thread binanceDataSender {
      [&context, &running] {
        data_transmission(context, running, exchange_e::binance, "binance");
      }
    };

    std::thread kucoinDataSender {
        [&context, &running]{
          data_transmission(context, running, exchange_e::kucoin, "kucoin");
        }
    };

    std::thread okDataSender {
        [&context, &running]{
          data_transmission(context, running, exchange_e::okex, "okex");
        }
    };

    binanceDataSender.join();
    kucoinDataSender.join();
    okDataSender.join();
  }
}
