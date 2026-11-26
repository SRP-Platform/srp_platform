/**
 * @file engine_service.h
 * @copyright Copyright (c) 2024
 * @brief Example: the EngineService offered over MAVLink (radio), skeleton side.
 *
 * Two instances of the same service (see example_app.json):
 *   Engine_red (instance 2): redundancy, every frame goes out on both "radio_a" and "radio_b";
 *   Engine_a   (instance 3): "radio_a" only, no SetMode (remote callers get status=2), no
 *              DebugDump, Telemetry at most 1 Hz.
 * Elements without a "mavlink" block in service.json (Reboot) or with "mavlink": false (Trace)
 * never reach the radio.
 */
#ifndef EXAMPLE_APP_EXAMPLEAPP_CODE_APPLICATION_ENGINE_SERVICE_H_
#define EXAMPLE_APP_EXAMPLEAPP_CODE_APPLICATION_ENGINE_SERVICE_H_
#include <iostream>
#include <mutex>  // NOLINT
#include <sstream>
#include <string>

#include "ara/log/log.h"
#include "srp/example/ExampleDataStructure.h"
#include "srp/example/EngineServiceSkeleton.h"

namespace srp {
namespace example {
/// Instances serve methods on separate threads: one whole line per write, under a mutex.
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
    return mode;  // the response echoes the mode
  }
  ara::core::Result<void> DebugDump() override {
    PrintLine("REQ DebugDump instance=" + std::to_string(id_));
    return {};
  }
  ara::core::Result<void> Reboot() override { return {}; }

 private:
  const int id_;
};

/// The two EngineService instances of the example (see example_app.json) and the events they
/// publish. Constructing starts offering them; Tick() is called every 10 ms.
class EngineInstances {
 public:
  EngineInstances()
      : red_{ara::core::InstanceSpecifier{"srp/example/ExampleApp/Engine_red"}, 2},
        a_{ara::core::InstanceSpecifier{"srp/example/ExampleApp/Engine_a"}, 3} {
    red_.StartOffer();
    a_.StartOffer();
  }
  ~EngineInstances() {
    red_.StopOffer();
    a_.StopOffer();
  }

  void Tick(uint32_t tick) {
    // Telemetry runs at 100 Hz; the max_rate_hz of an instance decides how much reaches the
    // radio (Engine_a: 1 Hz, Engine_red: the 2 Hz limit of the service).
    const srp::example::EngineTelemetry t{static_cast<uint16_t>(tick), 21.5F};
    red_.Telemetry.Update(t);
    a_.Telemetry.Update(t);
    if (tick % 10 == 0) {  // CurrentMode at 10 Hz
      const srp::example::EngineMode m{static_cast<uint8_t>(tick / 10)};
      red_.CurrentMode.Update(m);
      a_.CurrentMode.Update(m);
      red_.Trace.Update(1);  // "mavlink": false, so it never reaches the radio
    }
  }

 private:
  MyEngineService red_;
  MyEngineService a_;
};
}  // namespace example
}  // namespace srp
#endif  // EXAMPLE_APP_EXAMPLEAPP_CODE_APPLICATION_ENGINE_SERVICE_H_
