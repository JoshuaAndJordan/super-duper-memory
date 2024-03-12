#include "time_based_watch.hpp"
#include "dbus/use_cases/price_task_result_client_impl.hpp"
#include "price_stream/adaptor/commodity_adaptor.hpp"

#include <boost/asio/deadline_timer.hpp>
#include <thread>

using keep_my_journal::instrument_exchange_set_t;
extern instrument_exchange_set_t uniqueInstruments;

namespace keep_my_journal {
using dbus_timed_based_struct_t = dbus::adaptor::dbus_time_task_t;

struct scheduled_time_task_result_t {
  dbus::adaptor::dbus_time_task_t task;
  std::vector<dbus::adaptor::dbus_instrument_type_t> tokens;
};

utils::waitable_container_t<scheduled_time_task_result_t>
    scheduled_task_results{};

inline void send_price_task_result(scheduled_time_task_result_t &&res) {
  scheduled_task_results.append(std::move(res));
}

class time_based_watch_price_t::time_based_watch_price_impl_t
    : public std::enable_shared_from_this<time_based_watch_price_impl_t> {
  net::io_context &m_ioContext;
  utils::unique_elements_t<instrument_type_t> &m_instruments;
  scheduled_price_task_t const m_task;
  dbus::adaptor::dbus_time_task_t const m_dbusTask;
  std::optional<net::deadline_timer> m_timer = std::nullopt;

  void next_timer();

public:
  time_based_watch_price_impl_t(net::io_context &ioContext,
                                scheduled_price_task_t const &task)
      : m_ioContext(ioContext), m_instruments(uniqueInstruments[task.exchange]),
        m_task(task),
        m_dbusTask(dbus::adaptor::scheduled_task_to_dbus_time(task)) {}

  ~time_based_watch_price_impl_t() { stop(); }
  scheduled_price_task_t task_data() const { return m_task; }

  void call();
  void stop();
  void fetch_prices();
};

void time_based_watch_price_t::time_based_watch_price_impl_t::next_timer() {
  if (!m_timer)
    return;

  m_timer->expires_from_now(
      boost::posix_time::milliseconds(m_task.timeProp->timeMS));
  m_timer->async_wait(
      [self = shared_from_this()](boost::system::error_code const ec) {
        if (ec)
          return;
        self->fetch_prices();
      });
}

void time_based_watch_price_t::time_based_watch_price_impl_t::fetch_prices() {
  auto const instruments = m_instruments.to_list();
  scheduled_time_task_result_t data;
  data.tokens.reserve(m_task.tokens.size());

  for (auto const &instrument : m_task.tokens) {
    auto const iter =
        std::find_if(instruments.cbegin(), instruments.cend(),
                     [instrument, this](instrument_type_t const &instr) {
                       return instr.tradeType == m_task.tradeType &&
                              instr.name == instrument;
                     });
    if (iter != instruments.cend()) {
      data.tokens.emplace_back(iter->name, iter->currentPrice, iter->open24h,
                               (int)iter->tradeType);
    }
  }

  if (!data.tokens.empty()) {
    data.task = m_dbusTask;
    send_price_task_result(std::move(data));
  }

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
    : m_impl(std::make_shared<time_based_watch_price_impl_t>(ioContext, task)) {
}

void time_based_watch_price_t::run() {
  if (m_impl)
    m_impl->call();
}

scheduled_price_task_t time_based_watch_price_t::task_data() const {
  if (!m_impl)
    return scheduled_price_task_t{};
  return m_impl->task_data();
}

void time_based_watch_price_t::stop() {
  if (m_impl)
    m_impl->stop();
}

net::io_context &get_io_context() {
  static net::io_context ioContext((int)std::thread::hardware_concurrency());
  return ioContext;
}

void start_io_context_if_stopped() {
  auto &ioContext = get_io_context();
  if (ioContext.stopped())
    ioContext.restart();
  ioContext.run();
}

utils::locked_map_t<std::string,
                    std::vector<std::shared_ptr<time_based_watch_price_t>>>
    global_task_list{};

bool schedule_new_time_task_impl(scheduled_price_task_t const &taskInfo) {
  auto task =
      std::make_shared<time_based_watch_price_t>(get_io_context(), taskInfo);
  global_task_list[taskInfo.user_id].push_back(task);
  task->run();
  static bool result = [] {
    std::thread(start_io_context_if_stopped).detach();
    return true;
  }();
  return result;
}

void remove_scheduled_time_task_impl(std::string const &user_id,
                                     std::string const &task_id) {
  auto &task_list = global_task_list[user_id];
  if (task_list.empty())
    return;

  auto lambda = [user_id,
                 task_id](std::shared_ptr<time_based_watch_price_t> const &t) {
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

std::vector<dbus_timed_based_struct_t>
get_scheduled_tasks_for_user_impl(std::string const &user_id) {
  std::vector<dbus_timed_based_struct_t> result;
  if (auto const tasks = global_task_list.find_value(user_id);
      tasks.has_value()) {
    result.reserve(tasks->size());
    for (auto const &task : *tasks) {
      result.push_back(
          dbus::adaptor::scheduled_task_to_dbus_time(task->task_data()));
    }
  }

  return result;
}

std::vector<dbus_timed_based_struct_t> get_all_scheduled_tasks_impl() {
  static auto func = [](std::shared_ptr<time_based_watch_price_t> const &task) {
    return dbus::adaptor::scheduled_task_to_dbus_time(task->task_data());
  };

  std::vector<dbus_timed_based_struct_t> result;
  global_task_list.to_flat_list(result, func);
  return result;
}

inline dbus::adaptor::dbus_time_task_result_t
time_result_to_dbus_arg(scheduled_time_task_result_t &&task) {
  return dbus::adaptor::dbus_time_task_result_t{std::move(task.task),
                                                std::move(task.tokens)};
}

void result_sender_callback(bool &isRunning) {
  prices_result_proxy_impl_t result_proxy("keep.my.journal.prices.result",
                                          "/keep/my/journal/prices/result/1");
  while (isRunning) {
    auto result = scheduled_task_results.get();
    result_proxy.broadcast_time_price_result(
        time_result_to_dbus_arg(std::move(result)));
  }
}
} // namespace keep_my_journal