/**
 * @file engine_service.h
 * @copyright Copyright (c) 2024
 * @brief Przyklad: usluga EngineService wystawiona po MAVLinku (radio) - skeleton.
 *
 * Trzy instancje tej samej uslugi (patrz example_app.json):
 *   Engine_red  (instancja 2) - redundancja: kazda ramka idzie lacznie "lora" i "wifi",
 *   Engine_lora (instancja 3) - tylko radio "lora", bez DebugDump, Telemetry max 1 Hz,
 *   Engine_wifi (instancja 4) - tylko "wifi", bez SetMode (zdalnie: status=2), Telemetry max 20 Hz.
 * Elementy bez bloku "mavlink" w service.json (Reboot) albo z "mavlink": false (Trace) nie wychodza na radio.
 */
#ifndef EXAMPLE_APP_EXAMPLEAPP_CODE_APPLICATION_ENGINE_SERVICE_H_
#define EXAMPLE_APP_EXAMPLEAPP_CODE_APPLICATION_ENGINE_SERVICE_H_
#include <iostream>

#include "ara/log/log.h"
#include "srp/example/EngineServiceSkeleton.h"

namespace srp {
namespace example {
class MyEngineService : public srp::example::EngineServiceSkeleton {
 public:
  MyEngineService(const ara::core::InstanceSpecifier& instance, int id)
      : srp::example::EngineServiceSkeleton{instance}, id_{id} {}
  ~MyEngineService() = default;

 protected:
  ara::core::Result<srp::example::EngineMode> SetMode(
      const srp::example::EngineMode& mode) override {
    std::cout << "REQ SetMode instance=" << id_ << " mode=" << static_cast<int>(mode.mode) << std::endl;
    return mode;  // odpowiedz: ten sam tryb
  }
  ara::core::Result<void> DebugDump() override {
    std::cout << "REQ DebugDump instance=" << id_ << std::endl;
    return {};
  }
  ara::core::Result<void> Reboot() override { return {}; }

 private:
  const int id_;
};
}  // namespace example
}  // namespace srp
#endif  // EXAMPLE_APP_EXAMPLEAPP_CODE_APPLICATION_ENGINE_SERVICE_H_
