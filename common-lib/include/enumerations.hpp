#pragma once

namespace jordan {

enum class task_state_e : size_t {
  initiated,
  running,
  stopped,
  restarted,
  remove,
  unknown,
};

enum class task_type_e : size_t {
  profit_and_loss,
  price_changes,
  unknown,
};

enum class trade_direction_e : size_t {
  sell,
  buy,
  none,
};

enum class trade_type_e : size_t {
  futures,
  spot,
  swap,
  total,
};

enum class exchange_e : size_t {
  binance = 0,
  kucoin,
  okex,
  total,
};

enum class social_channel_e : size_t {
  telegram,
  whatsapp,
  email,
  none,
};
} // namespace jordan
