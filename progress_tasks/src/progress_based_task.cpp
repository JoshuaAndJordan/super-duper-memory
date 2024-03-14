#include "progress_based_task.hpp"
#include "dbus/use_cases/price_task_result_client_impl.hpp"
#include "price_stream/adaptor/commodity_adaptor.hpp"

#include <boost/asio/deadline_timer.hpp>
#include <thread>

using keep_my_journal::instrument_exchange_set_t;
extern instrument_exchange_set_t uniqueInstruments;

namespace keep_my_journal {
using dbus_progress_struct_t = dbus::adaptor::dbus_progress_struct_t;

struct scheduled_progress_task_result_t {
  dbus::adaptor::dbus_progress_struct_t task;
  std::vector<dbus::adaptor::dbus_instrument_type_t> tokens;
};

utils::waitable_container_t<scheduled_progress_task_result_t>
    scheduled_task_results{};

inline void send_price_task_result(scheduled_progress_task_result_t &&res) {
  scheduled_task_results.append(std::move(res));
}

inline bool is_lesser_or_equals(double const a, double const b) {
  return a <= b;
}

inline bool is_greater_or_equals(double const a, double const b) {
  return a >= b;
}

class progress_based_watch_price_t::progress_based_watch_price_impl_t
    : public std::enable_shared_from_this<progress_based_watch_price_impl_t> {
  using progress_comparator_t = bool (*)(double, double);

  net::io_context &m_ioContext;
  utils::unique_elements_t<instrument_type_t> &m_instruments;
  scheduled_price_task_t const m_task;
  dbus::adaptor::dbus_progress_struct_t const m_dbusTask;
  std::vector<instrument_type_t> m_snapshots;
  std::optional<net::deadline_timer> m_periodicTimer = std::nullopt;
  progress_comparator_t m_comparator = nullptr;

public:
  progress_based_watch_price_impl_t(net::io_context &ioContext,
                                    scheduled_price_task_t const &task)
      : m_ioContext(ioContext), m_instruments(uniqueInstruments[task.exchange]),
        m_task(task),
        m_dbusTask(dbus::adaptor::scheduled_task_to_dbus_progress(task)) {
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

    m_comparator =
        (percentage < 0.0) ? is_lesser_or_equals : is_greater_or_equals;
    assert(m_snapshots.size() == task.tokens.size());
  }

  void next_timer() {
    if (!m_periodicTimer)
      return;

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

  scheduled_price_task_t task_data() const { return m_task; }

  void check_prices() {
    scheduled_progress_task_result_t result;

    for (auto const &instrument : m_snapshots) {
      auto optInstr = m_instruments.find_item(instrument);
      assert(optInstr.has_value());
      if (!optInstr.has_value())
        continue;

      if (m_comparator(instrument.currentPrice, optInstr->currentPrice))
        result.tokens.emplace_back(instrument.name, instrument.currentPrice,
                                   instrument.open24h);
    }

    if (!result.tokens.empty()) {
      for (auto const &instrument : result.tokens) {
        m_snapshots.erase(
            std::remove_if(m_snapshots.begin(), m_snapshots.end(),
                           [instrument](instrument_type_t const &instr) {
                             return instrument.get<0>() == instr.name;
                           }),
            m_snapshots.end());
      }

      // send notification
      result.task = m_dbusTask;
      send_price_task_result(std::move(result));
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
    : m_impl(std::make_shared<progress_based_watch_price_impl_t>(ioContext,
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

scheduled_price_task_t progress_based_watch_price_t::task_data() const {
  if (!m_impl)
    return scheduled_price_task_t{};
  return m_impl->task_data();
}

net::io_context &get_io_context() {
  static net::io_context ioContext((int)std::thread::hardware_concurrency());
  return ioContext;
}

utils::locked_map_t<std::string,
                    std::vector<std::shared_ptr<progress_based_watch_price_t>>>
    global_task_list{};

void start_io_context_if_stopped() {
  auto &ioContext = get_io_context();
  if (ioContext.stopped())
    ioContext.restart();
  ioContext.run();
}

bool schedule_new_progress_task_impl(scheduled_price_task_t const &taskInfo) {
  auto task = std::make_shared<progress_based_watch_price_t>(get_io_context(),
                                                             taskInfo);
  global_task_list[taskInfo.user_id].push_back(task);
  task->run();
  static bool result = [] {
    std::thread(start_io_context_if_stopped).detach();
    return true;
  }();
  return result;
}

void remove_scheduled_progress_task_impl(std::string const &user_id,
                                         std::string const &task_id) {
  auto &task_list = global_task_list[user_id];
  if (task_list.empty())
    return;

  auto lambda = [user_id, task_id](
                    std::shared_ptr<progress_based_watch_price_t> const &t) {
    auto const task_data = t->task_data();
    return (task_data.user_id == user_id) && (task_data.task_id == task_id);
  };

  auto iter = std::find_if(task_list.begin(), task_list.end(), lambda);
  while (iter != task_list.end()) {
    (*iter)->stop();
    task_list.erase(iter);
    iter = std::find_if(task_list.begin(), task_list.end(), lambda);
  }
}

std::vector<dbus_progress_struct_t>
get_scheduled_tasks_for_user_impl(std::string const &user_id) {
  std::vector<dbus_progress_struct_t> result{};
  if (auto const tasks = global_task_list.find_value(user_id);
      tasks.has_value()) {
    result.reserve(tasks->size());
    for (auto const &task : *tasks) {
      result.push_back(
          dbus::adaptor::scheduled_task_to_dbus_progress(task->task_data()));
    }
  }

  return result;
}

std::vector<dbus_progress_struct_t> get_all_scheduled_tasks_impl() {
  static auto func =
      [](std::shared_ptr<progress_based_watch_price_t> const &task) {
        return dbus::adaptor::scheduled_task_to_dbus_progress(
            task->task_data());
      };

  std::vector<dbus_progress_struct_t> result;
  global_task_list.to_flat_list(result, func);
  return result;
}

inline dbus::adaptor::dbus_progress_task_result_t
progress_result_to_dbus_arg(scheduled_progress_task_result_t &&task) {
  return dbus::adaptor::dbus_progress_task_result_t{std::move(task.task),
                                                    std::move(task.tokens)};
}

void progress_result_sender_callback(bool &isRunning) {
  prices_result_proxy_impl_t result_proxy("keep.my.journal.prices.result",
                                          "/keep/my/journal/prices/result/1");
  while (isRunning) {
    auto result = scheduled_task_results.get();
    result_proxy.broadcast_progress_price_result(
        progress_result_to_dbus_arg(std::move(result)));
  }
}

} // namespace keep_my_journal