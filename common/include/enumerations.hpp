#pragma once

#ifdef CRYPTOLOG_USING_MSGPACK
#include <msgpack.hpp>
#endif

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

#ifdef CRYPTOLOG_USING_MSGPACK
MSGPACK_ADD_ENUM(jordan::task_state_e);
MSGPACK_ADD_ENUM(jordan::task_type_e);
MSGPACK_ADD_ENUM(jordan::trade_direction_e);
MSGPACK_ADD_ENUM(jordan::trade_type_e);
MSGPACK_ADD_ENUM(jordan::exchange_e);
MSGPACK_ADD_ENUM(jordan::social_channel_e);
#endif
