/**
 * @file ExampleApp2.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-27
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "example_app2/ExampleApp2/code/application/ExampleApp.h"

#include <iostream>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <utility>

#include "ara/log/log.h"
#include "core/common/condition.h"
#include "srp/example/ExampleDataStructure.h"
#include "srp/example/EngineService/EngineServiceHandler.h"
#include "srp/example/ExampleService/ExampleServiceHandler.h"

namespace srp {
namespace example {
namespace {
/// The receive handlers and the call loop print from different threads: one whole line per
/// write, under one mutex.
void PrintLine(const std::string& line) {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock{mutex};
  std::cout << line << std::endl;
}
}  // namespace

ExampleApp::ExampleApp() {}
ExampleApp::~ExampleApp() {}

int ExampleApp::Initialize(
    const std::map<ara::core::StringView, ara::core::StringView> parms) {
  ara::log::LogInfo() << "App Initialized";
  return 0;
}

int ExampleApp::Run(const std::stop_token& token) {
  ara::log::LogInfo() << "App start";
  srp::example::ExampleServiceProxy proxy1{
      ara::core::InstanceSpecifier{"srp/example/ExampleApp2/service3"}};
  proxy1.StartFindService([this](auto handler) {
    ara::log::LogInfo() << "Proxy Found";
    this->handler_ = std::move(handler);
    this->handler_->Status.Subscribe(1, [this](auto status) {
      ara::log::LogInfo() << "Subscribe status" << static_cast<uint8_t>(status);
      this->handler_->Status.SetReceiveHandler([this]() {
        const auto val = this->handler_->Status.GetNewSamples();
        if (val.HasValue()) {
          ara::log::LogInfo() << "Event new Value: " << val.Value();
        } else {
          ara::log::LogInfo() << "Error1";
        }
      });
    });
  });
  // One proxy of EngineService (see example_app.json): instance 2 is redundant, reachable over
  // radio_a and radio_b. The links and radio drivers are in the MavlinkDaemon config.
  srp::example::EngineServiceProxy engine{
      ara::core::InstanceSpecifier{"srp/example/ExampleApp2/Engine_red_in"}};
  std::mutex mutex;
  std::shared_ptr<srp::example::EngineServiceHandler> engine_handler;
  engine.StartFindService([&mutex, &engine_handler](auto handler) {
    std::lock_guard<std::mutex> lock{mutex};
    engine_handler = std::move(handler);
    auto h = engine_handler;
    h->CurrentMode.Subscribe(1, [h](auto) {
      h->CurrentMode.SetReceiveHandler([h]() {
        const auto v = h->CurrentMode.GetNewSamples();
        if (v.HasValue()) {
          PrintLine("EVT CurrentMode mode=" + std::to_string(static_cast<int>(v.Value().mode)));
        }
      });
    });
    h->Telemetry.Subscribe(1, [h](auto) {
      h->Telemetry.SetReceiveHandler([h]() {
        const auto v = h->Telemetry.GetNewSamples();
        if (v.HasValue()) {
          PrintLine("EVT Telemetry rpm=" + std::to_string(v.Value().rpm));
        }
      });
    });
  });

  uint32_t tick = 0;
  ara::log::LogInfo() << "App started";
  while (!token.stop_requested()) {
    core::condition::wait_for(std::chrono::milliseconds(500), token);
    ++tick;
    std::lock_guard<std::mutex> lock{mutex};
    if (!engine_handler) {
      continue;
    }
    const auto r = engine_handler->SetMode(srp::example::EngineMode{static_cast<uint8_t>(tick)});
    if (r.HasValue()) {
      PrintLine("CALL SetMode ok mode=" + std::to_string(static_cast<int>(r.Value().mode)));
    } else {
      PrintLine("CALL SetMode err=" + std::to_string(r.Error().Value()));
    }
  }
  ara::log::LogInfo() << "App Stop";
  return 0;
}

}  // namespace example
}  // namespace srp
