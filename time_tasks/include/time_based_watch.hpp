#pragma once

#include "price_stream/tasks.hpp"

namespace boost::asio {
class io_context;
}

namespace keep_my_journal {
namespace net = boost::asio;
class time_based_watch_price_t
    : public std::enable_shared_from_this<time_based_watch_price_t> {
  class time_based_watch_price_impl_t;

  std::shared_ptr<time_based_watch_price_impl_t> m_impl = nullptr;

public:
  time_based_watch_price_t(net::io_context &, scheduled_price_task_t const &);
  void run();
  void stop();
  scheduled_price_task_t task_data() const;
};

} // namespace keep_my_journal