// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include "price_tasks.hpp"
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <optional>

namespace net = boost::asio;

namespace keep_my_journal {
class time_based_watch_price_t::time_based_watch_price_impl_t {
  net::io_context &m_ioContext;
  utils::locked_set_t<instrument_type_t> &m_instruments;
  scheduled_price_task_t const m_task;
  std::optional<net::deadline_timer> m_timer = std::nullopt;

  void next_timer() {
    m_timer->expires_from_now(
        boost::posix_time::milliseconds(m_task.timeProp->timeMS));
    m_timer->async_wait([this](boost::system::error_code const ec) {
      if (ec)
        return;
      fetch_prices();
    });
  }

public:
  time_based_watch_price_impl_t(net::io_context &ioContext,
                                scheduled_price_task_t const &task)
      : m_ioContext(ioContext),
        m_instruments(instrument_sink_t::get_unique_instruments(task.exchange)),
        m_task(task) {}

  ~time_based_watch_price_impl_t() { stop(); }

  void call();
  void stop();
  void fetch_prices();
};

void time_based_watch_price_t::time_based_watch_price_impl_t::fetch_prices() {
  auto const instruments = m_instruments.to_list();
  scheduled_price_task_result_t result;
  result.result.reserve(m_task.tokens.size());

  for (auto const &instrument : m_task.tokens) {
    auto const iter =
        std::find_if(instruments.cbegin(), instruments.cend(),
                     [instrument, this](instrument_type_t const &instr) {
                       return instr.tradeType == m_task.tradeType &&
                              instr.name == instrument;
                     });
    if (iter != instruments.cend())
      result.result.push_back(*iter);
  }

  if (!result.result.empty()) {
    result.task = m_task;
    send_price_task_result(result);
  }

  // do something with the result and then schedule next timer call
  next_timer();
}

void time_based_watch_price_t::time_based_watch_price_impl_t::call() {
  if (m_timer)
    return;

  m_timer.emplace(m_ioContext);
  next_timer();
}

void time_based_watch_price_t::time_based_watch_price_impl_t::stop() {
  if (m_timer) {
    m_timer->cancel();
    m_timer.reset();
  }
}

time_based_watch_price_t::time_based_watch_price_t(
    net::io_context &ioContext, scheduled_price_task_t const &task)
    : price_task_t(ioContext),
      m_impl(new time_based_watch_price_impl_t(m_ioContext, task)) {}

void time_based_watch_price_t::call() {
  if (m_impl)
    m_impl->call();
}

void time_based_watch_price_t::stop() {
  if (m_impl)
    m_impl->stop();
}

time_based_watch_price_t::~time_based_watch_price_t() noexcept {
  delete m_impl;
}

// ===================================================================
class progress_based_watch_price_t::progress_based_watch_price_impl_t {
  net::io_context &m_ioContext;
  utils::locked_set_t<instrument_type_t> &m_instruments;
  scheduled_price_task_t const m_task;
  std::vector<instrument_type_t> m_snapshots;
  std::optional<net::deadline_timer> m_periodicTimer = std::nullopt;
  bool m_isLesserThanZero = false;

public:
  progress_based_watch_price_impl_t(net::io_context &ioContext,
                                    scheduled_price_task_t const &task)
      : m_ioContext(ioContext),
        m_instruments(instrument_sink_t::get_unique_instruments(task.exchange)),
        m_task(task) {
    auto const snapshot = m_instruments.to_list();
    m_snapshots.reserve(task.tokens.size());

    auto const percentage = task.percentProp->percentage;
    for (auto const &token : task.tokens) {
      auto const iter = std::find_if(
          snapshot.cbegin(), snapshot.cend(),
          [token, trade = task.tradeType](instrument_type_t const &instr) {
            return instr.tradeType == trade && instr.name == token;
          });

      if (iter != snapshot.cend()) {
        auto instr = *iter;
        auto const t = (instr.currentPrice * (percentage / 100.0));
        if (percentage < 0)
          instr.currentPrice -= t;
        else
          instr.currentPrice += t;
        m_snapshots.push_back(instr);
      }
    }

    m_isLesserThanZero = percentage < 0.0;
    assert(m_snapshots.size() == task.tokens.size());
  }

  void next_timer() {
    m_periodicTimer->expires_from_now(boost::posix_time::milliseconds(100));
    m_periodicTimer->async_wait([this](boost::system::error_code const ec) {
      if (ec)
        return;
      check_prices();
    });
  }

  void call() {
    if (m_periodicTimer)
      return;

    m_periodicTimer.emplace(m_ioContext);
    next_timer();
  }

  void check_prices() {
    scheduled_price_task_result_t result;

    for (auto const &instrument : m_snapshots) {
      auto optInstr = m_instruments.find_item(instrument);
      assert(optInstr.has_value());
      if (!optInstr.has_value())
        continue;

      if (m_isLesserThanZero) {
        if (instrument.currentPrice <= optInstr->currentPrice)
          result.result.push_back(instrument);
      } else {
        if (instrument.currentPrice >= optInstr->currentPrice)
          result.result.push_back(instrument);
      }
    }

    if (!result.result.empty()) {
      for (auto const &instrument : result.result) {
        m_snapshots.erase(
            std::remove_if(m_snapshots.begin(), m_snapshots.end(),
                           [instrument](instrument_type_t const &instr) {
                             return instrument.tradeType == instr.tradeType &&
                                    instrument.name == instr.name;
                           }),
            m_snapshots.end());
      }

      // send notification
      result.task = m_task;
      send_price_task_result(result);
    }

    // then stop the run when the snapshot's empty
    if (m_snapshots.empty())
      return stop();
    next_timer();
  }

  void stop() {
    if (m_periodicTimer) {
      m_periodicTimer->cancel();
      m_periodicTimer.reset();
    }
  }

  ~progress_based_watch_price_impl_t() { stop(); }
};

progress_based_watch_price_t::progress_based_watch_price_t(
    net::io_context &ioContext, scheduled_price_task_t const &task)
    : price_task_t(ioContext),
      m_impl(new progress_based_watch_price_impl_t(ioContext, task)) {}

progress_based_watch_price_t::~progress_based_watch_price_t() noexcept {
  delete m_impl;
}

void progress_based_watch_price_t::call() {
  if (m_impl)
    m_impl->call();
}

void progress_based_watch_price_t::stop() {
  if (m_impl)
    m_impl->stop();
}

bool passed_valid_task_check(scheduled_price_task_t &task) {
  if (task.tokens.empty() || !(task.percentProp || task.timeProp))
    return false;

  if (task.percentProp) {
    double const percentage =
        std::clamp(task.percentProp->percentage, -100.0, 100.0);
    if (percentage == 0.0)
      return false;
    task.percentProp->percentage = percentage;
  }

  if (task.timeProp && task.timeProp->timeMS <= 0)
    return false;

  if (task.exchange == exchange_e::total ||
      task.tradeType == trade_type_e::total)
    return false;

  return true;
}

net::io_context &get_io_context();

bool schedule_new_price_task(scheduled_price_task_t taskInfo) {
  if (!passed_valid_task_check(taskInfo))
    return false;

  auto &ioContext = get_io_context();
  std::unique_ptr<price_task_t> priceInfo = nullptr;

  if (taskInfo.percentProp) {
    priceInfo =
        std::make_unique<progress_based_watch_price_t>(ioContext, taskInfo);
  } else if (taskInfo.timeProp) {
    priceInfo = std::make_unique<time_based_watch_price_t>(ioContext, taskInfo);
  }

  if (!priceInfo)
    return false;

  global_price_task_sink_t::get_all_scheduled_tasks().append(
      std::move(priceInfo));
  return true;
}

void price_result_watcher(bool &isRunning) {
  auto &queue = global_price_task_sink_t::get_all_scheduled_tasks();
  while (isRunning) {
    auto result = queue.get();
  }
}

} // namespace keep_my_journal
