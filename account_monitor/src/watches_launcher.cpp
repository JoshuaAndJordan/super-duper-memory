#include <boost/asio/ssl/context.hpp>
#include <spdlog/spdlog.h>
#include <msgpack.hpp>
#include <cppzmq/zmq.hpp>
#include <thread>
#include <filesystem>

#include "account_stream/user_scheduled_task.hpp"

namespace net = boost::asio;
namespace ssl = net::ssl;

namespace jordan {
class binance_stream_t;
class kucoin_ua_stream_t;

using binance_stream_list_t = std::vector<std::shared_ptr<binance_stream_t>>;
using kucoin_stream_list_t = std::vector<std::shared_ptr<kucoin_ua_stream_t>>;

void addBinanceAccountStream(binance_stream_list_t &, account_info_t const &,
                             net::io_context& ioContext, ssl::context &sslContext);
void removeBinanceAccountStream(binance_stream_list_t&, account_info_t const &);
void addKucoinAccountStream(
    kucoin_stream_list_t &, account_info_t const &, trade_type_e const,
    net::io_context& ioContext, ssl::context &sslContext);
void removeKucoinAccountStream(kucoin_stream_list_t &, account_info_t const &);

void externalAccountMessageMonitor(
    net::io_context &ioContext, ssl::context &sslContext, bool &isRunning) {
  auto const path = "/tmp/cryptolog/stream/account";
  if (!std::filesystem::exists(path))
    std::filesystem::create_directories(path);

  zmq::context_t context { (int) std::thread::hardware_concurrency() };
  zmq::socket_t receiverSocket(context, zmq::socket_type::sub);

  auto const address = fmt::format("ipc://{}/task", path);
  receiverSocket.connect(address);

  binance_stream_list_t binanceStreams{};
  kucoin_stream_list_t kucoinStreams{};

  while (isRunning) {
    zmq::message_t message{};
    if (auto const optRecv = receiverSocket.recv(message); !optRecv.has_value()){
      spdlog::error("unable to receive valid message from socket");
      continue;
    }

    auto const oh =
    msgpack::unpack((char const *)message.data(), message.size());
    auto object = oh.get();

    account_scheduled_task_t task{};
    object.convert(task);

    account_info_t info;
    info.passphrase = task.passphrase;
    info.secretKey = task.secretKey;
    info.apiKey = task.apiKey;
    info.userID = task.userID;

    if (task.exchange == exchange_e::binance) {
      if (task.operation == task_operation_e::add)
        addBinanceAccountStream(binanceStreams, info, ioContext, sslContext);
      else if (task.operation == task_operation_e::remove)
        removeBinanceAccountStream(binanceStreams, info);
    } else if (task.exchange == exchange_e::kucoin) {
      if (task.operation == task_operation_e::add)
        addKucoinAccountStream(kucoinStreams, info, task.tradeType, ioContext, sslContext);
      else if (task.operation == task_operation_e::remove)
        removeKucoinAccountStream(kucoinStreams, info);
    }
  }
}

} // namespace jordan
