#include "price_stream/adaptor/scheduled_task_adaptor.hpp"

namespace keep_my_journal::dbus::adaptor {
dbus_progress_struct_t
scheduled_task_to_dbus_progress(scheduled_price_task_t const &taskInfo) {
  dbus_progress_struct_t arg = std::tuple(
      taskInfo.process_assigned_id, taskInfo.percentProp->percentage,
      (int32_t)taskInfo.percentProp->direction, (uint32_t)taskInfo.tradeType,
      (uint32_t)taskInfo.exchange, (uint32_t)taskInfo.status, taskInfo.task_id,
      taskInfo.user_id, taskInfo.tokens);
  return arg;
}

dbus_time_task_t
scheduled_task_to_dbus_time(scheduled_price_task_t const &taskInfo) {
  dbus_time_task_t arg = static_cast<dbus_time_task_t>(std::tuple(
      taskInfo.process_assigned_id, taskInfo.timeProp->timeMS,
      (int32_t)taskInfo.timeProp->duration, (int32_t)taskInfo.tradeType,
      (int32_t)taskInfo.exchange, (int32_t)taskInfo.status, taskInfo.task_id,
      taskInfo.user_id, taskInfo.tokens));
  return arg;
}

scheduled_price_task_t
dbus_time_to_scheduled_task(dbus_time_task_t const &taskStruct) {
  scheduled_price_task_t task{};
  task.process_assigned_id = taskStruct.get<0>();
  task.timeProp.emplace<scheduled_price_task_t::timed_based_property_t>({});
  task.timeProp->timeMS = taskStruct.get<1>();
  task.timeProp->duration = static_cast<duration_unit_e>(taskStruct.get<2>());
  task.tradeType = static_cast<trade_type_e>(taskStruct.get<3>());
  task.exchange = static_cast<exchange_e>(taskStruct.get<4>());
  task.status = static_cast<task_state_e>(taskStruct.get<5>());
  task.task_id = taskStruct.get<6>();
  task.user_id = taskStruct.get<7>();
  task.tokens = taskStruct.get<8>();
  return task;
}

scheduled_price_task_t
dbus_progress_to_scheduled_task(dbus_progress_struct_t const &taskStruct) {
  scheduled_price_task_t task{};
  task.process_assigned_id = taskStruct.get<0>();
  task.percentProp.emplace<scheduled_price_task_t::percentage_based_property_t>(
      {});
  task.percentProp->percentage = taskStruct.get<1>();
  task.percentProp->direction =
      static_cast<price_direction_e>(taskStruct.get<2>());
  task.tradeType = static_cast<trade_type_e>(taskStruct.get<3>());
  task.exchange = static_cast<exchange_e>(taskStruct.get<4>());
  task.status = static_cast<task_state_e>(taskStruct.get<5>());
  task.task_id = taskStruct.get<6>();
  task.user_id = taskStruct.get<7>();
  task.tokens = taskStruct.get<8>();
  return task;
}
} // namespace keep_my_journal::dbus::adaptor

namespace sdbus {
Message &operator<<(Message &msg, kmj_adaptor::progress_task_t const &task) {
  auto struct_signature = signature_of<kmj_adaptor::progress_task_t>::str();
  assert(struct_signature.size() > 8);
  // remove opening and closing parenthesis from signature
  auto struct_content_signature =
      struct_signature.substr(1, struct_signature.size() - 2);

  msg.openStruct(struct_content_signature);
  msg << task.process_assigned_id << task.percentage << task.direction_enum
      << task.trade_type_enum << task.exchange_enum << task.task_status_enum
      << task.task_id << task.user_id;

  msg.openContainer(sdbus::signature_of<std::vector<std::string>>::str());
  for (auto const &item : task.tokens)
    msg << item;
  msg.closeContainer();

  msg.closeStruct();
  return msg;
}

Message &operator<<(Message &msg, kmj_adaptor::time_based_task_t const &task) {
  auto struct_signature = signature_of<kmj_adaptor::time_based_task_t>::str();
  assert(struct_signature.size() > 8);
  // remove opening and closing parenthesis from signature
  auto struct_content_signature =
      struct_signature.substr(1, struct_signature.size() - 2);

  msg.openStruct(struct_content_signature);
  msg << task.process_assigned_id << task.time << task.duration_enum
      << task.trade_type_enum << task.exchange_enum << task.task_status_enum
      << task.task_id << task.user_id;

  msg.openContainer(sdbus::signature_of<std::vector<std::string>>::str());
  for (auto const &item : task.tokens)
    msg << item;
  msg.closeContainer();

  msg.closeStruct();
  return msg;
}

Message &operator>>(Message &message, kmj_adaptor::progress_task_t &task) {
  auto struct_signature = signature_of<kmj_adaptor::progress_task_t>::str();
  assert(struct_signature.size() > 8);
  // remove opening and closing parenthesis from signature
  auto struct_content_signature =
      struct_signature.substr(1, struct_signature.size() - 2);

  if (!message.enterStruct(struct_content_signature))
    return message;
  message >> task.process_assigned_id >> task.percentage >>
      task.direction_enum >> task.trade_type_enum >> task.exchange_enum >>
      task.task_status_enum >> task.task_id >> task.user_id;

  if (!message.enterContainer(signature_of<std::vector<std::string>>::str()))
    return message;
  while (true) {
    std::string value{};
    if (message >> value)
      task.tokens.emplace_back(std::move(value));
    else
      break;
  }
  message.clearFlags();
  message.exitContainer();
  message.exitStruct();
  return message;
}

Message &operator>>(Message &message, kmj_adaptor::time_based_task_t &task) {
  auto struct_signature = signature_of<kmj_adaptor::time_based_task_t>::str();
  assert(struct_signature.size() > 8);
  // remove opening and closing parenthesis from signature
  auto struct_content_signature =
      struct_signature.substr(1, struct_signature.size() - 2);

  if (!message.enterStruct(struct_content_signature))
    return message;
  message >> task.process_assigned_id >> task.time >> task.duration_enum >>
      task.trade_type_enum >> task.exchange_enum >> task.task_status_enum >>
      task.task_id >> task.user_id;

  if (!message.enterContainer(signature_of<std::vector<std::string>>::str()))
    return message;
  while (true) {
    std::string value{};
    if (message >> value)
      task.tokens.emplace_back(std::move(value));
    else
      break;
  }
  message.clearFlags();
  message.exitContainer();
  message.exitStruct();
  return message;
}

} // namespace sdbus