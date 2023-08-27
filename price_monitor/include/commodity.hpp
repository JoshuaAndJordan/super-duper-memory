#pragma once

#include "enumerations.hpp"
#include <map>
#include <set>
#include <string>

namespace jordan {

struct instrument_type_t {
  double current_price = 0.0;
  double open24h = 0.0;
  std::string name;

  friend bool operator<(instrument_type_t const &instA,
                        instrument_type_t const &instB) {
    return instA.name < instB.name;
  }
};

class instrument_sink_t {
public:
  static auto &get_all_listed_instruments() {
    static std::map<exchange_e,
                    std::map<jordan::trade_type_e, std::set<instrument_type_t>>>
        instrumentsSink;

    return instrumentsSink;
  }
};

} // namespace jordan
