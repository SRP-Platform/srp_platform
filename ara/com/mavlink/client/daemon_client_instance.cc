/**
 * Copyright (c) 2024
 *
 * @file daemon_client_instance.cc
 * @brief Klient procesu z logowaniem przez ara::log (osobna jednostka, żeby testy klienta nie
 * wymagały ara::log).
 */
#include <cstdlib>

#include "ara/com/log.h"
#include "ara/com/mavlink/client/daemon_client.h"
#include "ara/log/logging_menager.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
void GlobalLog(ClientLogLevel level, const std::string& msg) {
  if (ara::log::LoggingMenager::GetInstance() == nullptr) {
    return;  // logger jeszcze nie zainicjowany
  }
  switch (level) {
    case ClientLogLevel::kDebug:
    case ClientLogLevel::kInfo:
      ara::com::LogInfo() << msg;
      break;
    case ClientLogLevel::kWarn:
      ara::com::LogWarn() << msg;
      break;
    case ClientLogLevel::kError:
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
