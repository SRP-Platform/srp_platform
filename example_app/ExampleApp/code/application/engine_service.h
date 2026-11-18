/**
 * @file engine_service.h
 * @copyright Copyright (c) 2024
 * @brief Przyklad: usluga EngineService wystawiona po MAVLinku (radio) - skeleton.
 *
 * Trzy instancje tej samej uslugi (patrz example_app.json):
 *   Engine_red  (instancja 2) - redundancja: kazda ramka idzie lacznie laczami "radio_a" i "radio_b",
 *   Engine_a    (instancja 3) - tylko lacze "radio_a", bez DebugDump, Telemetry max 1 Hz,
 *   Engine_b    (instancja 4) - tylko lacze "radio_b", bez SetMode (zdalnie: status=2), Telemetry max 20 Hz.
 * Elementy bez bloku "mavlink" w service.json (Reboot) albo z "mavlink": false (Trace) nie wychodza na radio.
 */
#ifndef EXAMPLE_APP_EXAMPLEAPP_CODE_APPLICATION_ENGINE_SERVICE_H_
#define EXAMPLE_APP_EXAMPLEAPP_CODE_APPLICATION_ENGINE_SERVICE_H_
#include <iostream>
#include <mutex>  // NOLINT
#include <sstream>
#include <string>

#include "ara/log/log.h"
#include "srp/example/EngineServiceSkeleton.h"

namespace srp {
namespace example {
/// Instancje obsluguja metody na osobnych watkach: cala linia jednym zapisem pod mutexem (bez przeplotu wyjscia).
inline void PrintLine(const std::string& line) {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock{mutex};
  std::cout << line << std::endl;
}

class MyEngineService : public srp::example::EngineServiceSkeleton {
 public:
  MyEngineService(const ara::core::InstanceSpecifier& instance, int id)
      : srp::example::EngineServiceSkeleton{instance}, id_{id} {}
  ~MyEngineService() = default;

 protected:
  ara::core::Result<srp::example::EngineMode> SetMode(
      const srp::example::EngineMode& mode) override {
    std::ostringstream line;
    line << "REQ SetMode instance=" << id_ << " mode=" << static_cast<int>(mode.mode);
    PrintLine(line.str());
    return mode;  // odpowiedz: ten sam tryb
  }
  ara::core::Result<void> DebugDump() override {
    PrintLine("REQ DebugDump instance=" + std::to_string(id_));
    return {};
  }
  ara::core::Result<void> Reboot() override { return {}; }

 private:
  const int id_;
};
}  // namespace example
}  // namespace srp
#endif  // EXAMPLE_APP_EXAMPLEAPP_CODE_APPLICATION_ENGINE_SERVICE_H_
