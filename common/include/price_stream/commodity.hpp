// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include "container.hpp"
#include "enumerations.hpp"

#ifdef CRYPTOLOG_USING_MSGPACK
#include <msgpack.hpp>
#endif

#include <boost/functional/hash.hpp>
#include <map>
#include <string>

namespace keep_my_journal {
struct instrument_type_t {
  std::string name;
  double currentPrice = 0.0;
  double open24h = 0.0;
  trade_type_e tradeType;

#ifdef CRYPTOLOG_USING_MSGPACK
  MSGPACK_DEFINE(name, currentPrice, open24h, tradeType);
#endif
};

using instrument_list_t = utils::waitable_container_t<instrument_type_t>;
using instrument_set_t = utils::unique_elements_t<instrument_type_t>;
using instrument_exchange_set_t =
    utils::locked_map_t<keep_my_journal::exchange_e,
                        keep_my_journal::instrument_set_t>;

struct instrument_sink_t {
  using list_t = keep_my_journal::instrument_list_t;
  static list_t &get_all_listed_instruments(exchange_e const e) {
    static std::map<exchange_e, list_t> instrumentsSink{};
    return instrumentsSink[e];
  }
};

} // namespace keep_my_journal

namespace std {
template <> struct hash<keep_my_journal::instrument_type_t> {
  using argument_type = keep_my_journal::instrument_type_t;
  using result_type = std::size_t;

  result_type operator()(argument_type const &instrument) const {
    std::size_t val{};
    boost::hash_combine(val, instrument.name);
    boost::hash_combine(val, instrument.tradeType);
    return val;
  }
};

template <> struct equal_to<keep_my_journal::instrument_type_t> {
  using argument_type = keep_my_journal::instrument_type_t;

  bool operator()(argument_type const &instA,
                  argument_type const &instB) const {
    return std::tie(instA.name, instA.tradeType) ==
           std::tie(instB.name, instB.tradeType);
  }
};

template <> struct less<keep_my_journal::instrument_type_t> {
  using argument_type = keep_my_journal::instrument_type_t;

  bool operator()(argument_type const &instA,
                  argument_type const &instB) const {
    return std::tie(instA.name, instA.tradeType) <
           std::tie(instB.name, instB.tradeType);
  }
};

} // namespace std