// Copyright (C) 2023 Joshua and Jordan Ogunyinka

#include "scheduled_price_tasks.hpp"
#include "macro_defines.hpp"

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <cppzmq/zmq.hpp>
#include <map>
#include <optional>
#include <spdlog/spdlog.h>

namespace net = boost::asio;

namespace keep_my_journal {
struct scheduled_price_task_comparator_t {
  bool operator()(scheduled_price_task_t const &a,
                  scheduled_price_task_t const &b) const {
    return std::tie(a.user_id, a.task_id) < std::tie(b.user_id, b.task_id);
  }
};

utils::waitable_container_t<scheduled_price_task_result_t> global_result_list;
std::map<scheduled_price_task_t, std::shared_ptr<price_task_t>,
         scheduled_price_task_comparator_t>
    global_price_tasks;

class time_based_watch_price_t::time_based_watch_price_impl_t
    : public std::enable_shared_from_this<time_based_watch_price_impl_t> {
  net::io_context &m_ioContext;
  utils::locked_set_t<instrument_type_t> &m_instruments;
  scheduled_price_task_t const m_task;
  std::optional<net::deadline_timer> m_timer = std::nullopt;

  void next_timer() {
    m_timer->expires_from_now(
        boost::posix_time::milliseconds(m_task.timeProp->timeMS));
    m_timer->async_wait(
        [self = shared_from_this()](boost::system::error_code const ec) {
          if (ec)
            return;
          self->fetch_prices();
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
      m_impl(
          std::make_shared<time_based_watch_price_impl_t>(m_ioContext, task)) {}

void time_based_watch_price_t::run() {
  if (m_impl)
    m_impl->call();
}

void time_based_watch_price_t::stop() {
  if (m_impl)
    m_impl->stop();
}

// ===================================================================
class progress_based_watch_price_t::progress_based_watch_price_impl_t
    : public std::enable_shared_from_this<progress_based_watch_price_impl_t> {
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
    m_periodicTimer->async_wait(
        [self = shared_from_this()](boost::system::error_code const ec) {
          if (ec)
            return;
          self->check_prices();
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
      m_impl(std::make_shared<progress_based_watch_price_impl_t>(ioContext,
                                                                 task)) {}

void progress_based_watch_price_t::run() {
  if (m_impl)
    m_impl->call();
}

void progress_based_watch_price_t::stop() {
  if (m_impl) {
    m_impl->stop();
    m_impl.reset();
  }
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
  static auto &queue = global_price_task_sink_t::get_all_scheduled_tasks();
  std::shared_ptr<price_task_t> priceTask;

  if (taskInfo.percentProp) {
    priceTask =
        std::make_shared<progress_based_watch_price_t>(ioContext, taskInfo);
  } else if (taskInfo.timeProp) {
    priceTask = std::make_shared<time_based_watch_price_t>(ioContext, taskInfo);
  } else {
    return false;
  }

  if (priceTask) {
    priceTask->run();
    queue.insert(priceTask);

    taskInfo.status = task_state_e::running;
    global_price_tasks[taskInfo] = priceTask;
  }

  return priceTask != nullptr;
}

void send_price_task_result(scheduled_price_task_result_t const &result) {
  global_result_list.append(result);
}

void stop_scheduled_price_task(scheduled_price_task_t const &taskInfo) {
  auto taskIter = global_price_tasks.find(taskInfo);
  if (taskIter != global_price_tasks.end()) {
    taskIter->second->stop();
    global_price_tasks.erase(taskIter);
  }
}

std::vector<scheduled_price_task_t>
get_price_tasks_for_user(std::string const &userID) {
  std::vector<scheduled_price_task_t> taskList;

  for (auto const &[task, _] : global_price_tasks) {
    if (task.user_id == userID)
      taskList.push_back(task);
  }
  return taskList;
}

void price_result_list_watcher(bool &isRunning) {
  auto const address = fmt::format("ipc://{}", PRICE_MONITOR_TASK_RESULT_PATH);

  zmq::context_t msgContext;
  msgpack::sbuffer buffer;
  zmq::socket_t sendingSocket(msgContext, zmq::socket_type::pub);
  sendingSocket.bind(address);

  while (isRunning) {
    auto result = global_result_list.get();
    msgpack::pack(buffer, result);
    std::string_view const v(buffer.data(), buffer.size());

    zmq::message_t message(v);
    auto const optSize = sendingSocket.send(message, zmq::send_flags::none);
    if (!optSize.has_value())
      spdlog::error("unable to send message on address " + address);
  }
}
} // namespace keep_my_journal
