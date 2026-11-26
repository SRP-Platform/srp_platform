/**
 * Copyright (c) 2024
 *
 * @file daemon_client_instance.cc
 * @brief The process client logging through ara::log (a separate unit so that the client tests
 * do not need ara::log).
 */
#include <cstdlib>

#include "ara/com/log.h"
#include "ara/com/mavlink/client/daemon_client.h"
#include "ara/log/logging_menager.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
void GlobalLog(LogLevel level, const std::string& msg) {
  if (ara::log::LoggingMenager::GetInstance() == nullptr) {
    return;  // the logger is not initialised yet
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

DaemonClient::Options GlobalOptions() {
  DaemonClient::Options o;
  const char* path = std::getenv("SRP_MAVLINK_DAEMON_SOCKET");
  if (path != nullptr && *path != '\0') {
    o.socket_path = path;
  }
  o.log = GlobalLog;
  return o;
}
}  // namespace

DaemonClient& DaemonClient::Instance() {
  static DaemonClient instance{GlobalOptions()};
  return instance;
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
