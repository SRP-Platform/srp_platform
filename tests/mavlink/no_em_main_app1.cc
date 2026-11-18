/**
 * @file no_em_main_app1.cc
 * @copyright Copyright (c) 2024
 * @brief Test main for example_app: runs only the MAVLink part (the EngineService instances)
 * without Execution Management and without the SOME/IP service that example_app also offers;
 * neither exists on the host that runs the end-to-end tests.
 */
#include <chrono>  // NOLINT
#include <cstdint>
#include <map>
#include <stop_token>  // NOLINT

#include "ara/exec/adaptive_application.h"
#include "ara/exec/adaptive_lifecycle.h"
#include "core/common/condition.h"
#include "example_app/ExampleApp/code/application/engine_service.h"

namespace {
class EngineApp : public ara::exec::AdaptiveApplication {
 private:
  int Initialize(const std::map<ara::core::StringView, ara::core::StringView> parms) override {
    return 0;
  }
  int Run(const std::stop_token& token) override {
    srp::example::EngineInstances engines;
    uint32_t tick = 0;
    while (!token.stop_requested()) {
      srp::core::condition::wait_for(std::chrono::milliseconds(10), token);
      engines.Tick(++tick);
    }
    return 0;
  }
};
}  // namespace

int main(int argc, char const *argv[]) {
  return ara::exec::RunAdaptiveLifecycleWithoutControl<EngineApp>(argc, argv);
}
