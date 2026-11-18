/**
 * Copyright (c) 2024
 *
 * @file link_manager_instance.cc
 * @brief Manager procesu z logowaniem przez ara::log (osobna jednostka, żeby testy
 * jednostkowe LinkManager nie wymagały ara::log).
 */
#include "ara/com/log.h"
#include "ara/com/mavlink/link_manager.h"
#include "ara/log/logging_menager.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
void GlobalLog(LogLevel level, const std::string& msg) {
  if (ara::log::LoggingMenager::GetInstance() == nullptr) {
    return;  // logger jeszcze nie zainicjowany
  }
  switch (level) {
    case LogLevel::kDebug:
    case LogLevel::kInfo:
      ara::com::LogInfo() << msg;
      break;
    case LogLevel::kWarn:
      ara::com::LogWarn() << msg;
      break;
    case LogLevel::kError:
      ara::com::LogError() << msg;
      break;
  }
}

LinkManager::Options GlobalOptions() {
  LinkManager::Options o;
  o.log = GlobalLog;
  return o;
}
}  // namespace

LinkManager& LinkManager::Instance() {
  static LinkManager instance{GlobalOptions()};
  return instance;
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
