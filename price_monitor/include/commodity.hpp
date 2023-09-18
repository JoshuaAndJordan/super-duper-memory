#pragma once

#include "enumerations.hpp"
#include "container.hpp"

#ifdef CRYPTOLOG_USING_MSGPACK
#include <msgpack.hpp>
#endif

#include <map>
#include <string>

namespace jordan {
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
};

} // namespace jordan

namespace std {
    template<>
    struct hash <jordan::instrument_type_t> {
        std::size_t operator()(jordan::instrument_type_t const &inst) const noexcept {
            return std::hash<std::string>{}(inst.name);
        }
    };

    template<>
    struct equal_to<jordan::instrument_type_t> {
        bool operator()(
                jordan::instrument_type_t const &a,
                jordan::instrument_type_t const &b) const noexcept {
            return a.name == b.name;
        }
    };
}
