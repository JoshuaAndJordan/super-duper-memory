// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#ifdef CRYPTOLOG_USING_MSGPACK
#include <msgpack.hpp>
#endif

namespace keep_my_journal {

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

enum class price_direction_e : size_t {
  up,
  down,
  invalid,
};

enum class duration_unit_e : size_t {
  seconds,
  minutes,
  hours,
  days,
  weeks,
  invalid
};
} // namespace keep_my_journal

#ifdef CRYPTOLOG_USING_MSGPACK
MSGPACK_ADD_ENUM(keep_my_journal::task_state_e);
MSGPACK_ADD_ENUM(keep_my_journal::task_type_e);
MSGPACK_ADD_ENUM(keep_my_journal::trade_direction_e);
MSGPACK_ADD_ENUM(keep_my_journal::trade_type_e);
MSGPACK_ADD_ENUM(keep_my_journal::exchange_e);
MSGPACK_ADD_ENUM(keep_my_journal::social_channel_e);
MSGPACK_ADD_ENUM(keep_my_journal::price_direction_e);
MSGPACK_ADD_ENUM(keep_my_journal::duration_unit_e);
#endif
