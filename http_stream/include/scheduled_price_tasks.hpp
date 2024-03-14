// Copyright (C) 2023-2024 Joshua and Jordan Ogunyinka
#pragma once

#include "price_stream/tasks.hpp"

#ifdef CRYPTOLOG_USING_MSGPACK
#include <msgpack.hpp>
#endif

namespace keep_my_journal {
bool schedule_new_price_task(scheduled_price_task_t);
void stop_scheduled_price_task(scheduled_price_task_t const &taskInfo);
std::vector<scheduled_price_task_t>
get_price_tasks_for_user(std::string const &userID);
std::vector<scheduled_price_task_t> get_price_tasks_for_all();
bool push_progress_based_task_to_wire(scheduled_price_task_t &&);
bool push_time_based_task_to_wire(scheduled_price_task_t &&);
void send_telegram_registration_code(std::string const &mobile,
                                     std::string const &code);
void send_telegram_registration_password(std::string const &mobile,
                                         std::string const &code);
void send_new_telegram_text(int64_t chat_id, std::string const &content);
} // namespace keep_my_journal
