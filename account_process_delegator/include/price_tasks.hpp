// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include "price_stream/commodity.hpp"

namespace boost::asio {
class io_context;
}

namespace net = boost::asio;

namespace keep_my_journal {
struct scheduled_price_task_t {
  struct timed_based_property_t {
    uint64_t timeMS{};
  };

  struct percentage_based_property_t {
    double percentage{};
  };

  int task_id = 0;
  std::vector<std::string> tokens;
  trade_type_e tradeType = trade_type_e::total;
  exchange_e exchange = exchange_e::total;
  std::optional<percentage_based_property_t> percentProp = std::nullopt;
  std::optional<timed_based_property_t> timeProp = std::nullopt;
};

struct scheduled_price_task_result_t {
  scheduled_price_task_t task;
  std::vector<instrument_type_t> result;
};

class price_task_t {
protected:
  net::io_context &m_ioContext;

public:
  explicit price_task_t(net::io_context &ioContext) : m_ioContext(ioContext) {}
  virtual ~price_task_t() = default;
  virtual void call() = 0;
  virtual void stop() = 0;
};

class time_based_watch_price_t : public price_task_t {
  class time_based_watch_price_impl_t;

  time_based_watch_price_impl_t *m_impl = nullptr;

public:
  ~time_based_watch_price_t() noexcept override;
  time_based_watch_price_t(net::io_context &, scheduled_price_task_t const &);
  void call() override;
  void stop() override;
};

class progress_based_watch_price_t : public price_task_t {
  class progress_based_watch_price_impl_t;

  progress_based_watch_price_impl_t *m_impl = nullptr;

public:
  ~progress_based_watch_price_t() noexcept override;
  progress_based_watch_price_t(net::io_context &,
                               scheduled_price_task_t const &);
  void call() override;
  void stop() override;
};

class global_price_task_sink_t {
  friend bool schedule_new_price_task(scheduled_price_task_t);
  friend void price_result_watcher(bool &isRunning);
  static auto &get_all_scheduled_tasks() {
    static utils::waitable_container_t<std::unique_ptr<price_task_t>> tasks;
    return tasks;
  }
};

bool schedule_new_price_task(scheduled_price_task_t);
void send_price_task_result(scheduled_price_task_result_t const &);
} // namespace keep_my_journal
