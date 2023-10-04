// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include "container.hpp"
#include "enumerations.hpp"

#ifdef CRYPTOLOG_USING_MSGPACK
#include <msgpack.hpp>
#endif

#include <map>
#include <string>

namespace keep_my_journal {
struct instrument_type_t {
  std::string name;
  double currentPrice = 0.0;
  double open24h = 0.0;
  trade_type_e tradeType;

  friend bool operator<(instrument_type_t const &instA,
                        instrument_type_t const &instB) {
    return std::tie(instA.name, instA.tradeType) <
           std::tie(instB.name, instB.tradeType);
  }

#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(name, currentPrice, open24h, tradeType);
#endif
};

struct instrument_sink_t {
  using list_t = utils::waitable_container_t<instrument_type_t>;
  static auto &get_all_listed_instruments(exchange_e const e) {
    static std::map<exchange_e, list_t> instrumentsSink;
    return instrumentsSink[e];
  }

  static auto &get_unique_instruments(exchange_e const e) {
    static std::map<exchange_e, utils::locked_set_t<instrument_type_t>>
        instruments;
    return instruments[e];
  }
};

} // namespace keep_my_journal

namespace std {
template <> struct hash<keep_my_journal::instrument_type_t> {
  std::size_t
  operator()(keep_my_journal::instrument_type_t const &inst) const noexcept {
    return std::hash<std::string>{}(inst.name);
  }
};

template <> struct equal_to<keep_my_journal::instrument_type_t> {
  bool operator()(keep_my_journal::instrument_type_t const &a,
                  keep_my_journal::instrument_type_t const &b) const noexcept {
    return std::tie(a.name, a.tradeType) == std::tie(b.name, b.tradeType);
  }
};
} // namespace std
