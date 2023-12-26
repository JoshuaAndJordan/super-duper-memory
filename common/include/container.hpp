// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace keep_my_journal::utils {
template <typename Key, typename Value> struct locked_map_t {
private:
  std::map<Key, Value> m_map{};
  std::mutex m_mutex{};

public:
  auto &operator[](Key const &key) {
    std::lock_guard<std::mutex> lockGuard(m_mutex);
    return m_map[key];
  }
};

template <typename T, typename Hash = std::hash<T>,
          typename EqualTo = std::equal_to<T>>
struct unique_elements_t {
private:
  using map_type_t = std::unordered_map<T, T, Hash, EqualTo>;
  using set_type_t = std::unordered_set<T, Hash, EqualTo>;
  using set_iterator_t = typename set_type_t::iterator;
  using map_iterator_t = typename map_type_t::iterator;

  map_type_t m_set{};
  std::mutex m_mutex{};

  static constexpr bool const is_map_v =
      std::is_same_v<std::decay_t<decltype(m_set)>, map_type_t>;
  using iterator_t =
      std::conditional_t<is_map_v, map_iterator_t, set_iterator_t>;

  template <typename U> void insert_impl(U &&item) {
    if constexpr (is_map_v) {
      m_set[item] = std::forward<U>(item);
    } else {
      if (iterator_t iter = m_set.find(item); iter != m_set.end())
        m_set.erase(iter);
      m_set.insert(std::forward<U>(item));
    }
  }

public:
  ~unique_elements_t() = default;

  template <typename U> void insert(U &&item) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    // replace if exists, otherwise insert
    insert_impl(std::forward<U>(item));
  }

  template <typename Func> std::vector<T> all_items_matching(Func &&filter) {
    std::vector<T> items{};
    std::lock_guard<std::mutex> lock_g{m_mutex};
    for (auto const &item : m_set) {
      if (filter(item))
        items.push_back(item);
    }
    return items;
  }

  void clear() {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    m_set.clear();
  }

  bool empty() const {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    return m_set.empty();
  }

  std::vector<T> to_list() {
    std::lock_guard<std::mutex> lockGuard(m_mutex);
    if constexpr (is_map_v) {
      std::vector<T> list;
      list.reserve(m_set.size());
      for (auto const &[_, value] : m_set)
        list.emplace_back(value);
      return list;
    } else {
      return std::vector<T>(m_set.cbegin(), m_set.cend());
    }
  }

  std::optional<T> find_item(T const &item) {
    std::lock_guard<std::mutex> lockGuard(m_mutex);
    iterator_t iter = m_set.find(item);
    if (iter == m_set.end())
      return std::nullopt;

    if constexpr (is_map_v) {
      return iter->second;
    } else {
      return *iter;
    }
  }
};

template <typename T, typename Container = std::deque<T>>
struct waitable_container_t {
private:
  std::mutex m_mutex{};
  Container m_container{};
  std::condition_variable m_cv{};

public:
  explicit waitable_container_t(Container &&container)
      : m_container{std::move(container)} {}
  waitable_container_t() = default;

  waitable_container_t(waitable_container_t &&vec) noexcept
      : m_mutex{std::move(vec.m_mutex)},
        m_container{std::move(vec.m_container)}, m_cv{std::move(vec.m_cv)} {}
  waitable_container_t &operator=(waitable_container_t &&) = delete;
  waitable_container_t(waitable_container_t const &) = delete;
  waitable_container_t &operator=(waitable_container_t const &) = delete;
  ~waitable_container_t() = default;

  void clear() {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    m_container.clear();
  }

  bool empty() {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    return m_container.empty();
  }
  T get() {
    std::unique_lock<std::mutex> u_lock{m_mutex};
    m_cv.wait(u_lock, [this] { return !m_container.empty(); });
    if (m_container.empty()) // avoid spurious wakeup
      throw std::runtime_error("container is empty when it isn't supposed to");
    T value{std::move(m_container.front())};
    m_container.pop_front();
    return value;
  }

  template <typename U> void append(U &&data) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    m_container.push_back(std::forward<U>(data));
    m_cv.notify_all();
  }

  template <
      typename NewContainer,
      typename = std::enable_if_t<std::is_convertible_v<
          typename decltype(std::declval<NewContainer>().begin())::value_type,
          T>>>
  void append_list(NewContainer &&new_list) {
    using iter_t = typename NewContainer::iterator;

    std::lock_guard<std::mutex> lock_g{m_mutex};
    m_container.insert(std::end(m_container),
                       std::move_iterator<iter_t>(std::begin(new_list)),
                       std::move_iterator<iter_t>(std::end(new_list)));
    m_cv.notify_all();
  }
};

template <typename T> struct mutexed_list_t {
private:
  std::mutex m_mutex;
  std::list<T> m_list;

public:
  using value_type = T;
  mutexed_list_t() = default;

  template <typename U> void append(U &&data) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    m_list.emplace_back(std::forward<U>(data));
  }

  T get() {
    if (empty())
      throw std::runtime_error("container is empty");

    std::lock_guard<std::mutex> lock_g{m_mutex};
    T value{m_list.front()};
    m_list.pop_front();
    return value;
  }

  bool empty() {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    return m_list.empty();
  }
};
} // namespace keep_my_journal::utils
