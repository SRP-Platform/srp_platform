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
#include "example_app/ExampleApp/code/application/ExampleApp.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <map>
#include <utility>

#include "ara/com/mavlink/link_transport_registry.h"
#include "ara/com/mavlink/serial_transport.h"
#include "ara/log/log.h"
#include "core/common/condition.h"
#include "example_app/ExampleApp/code/application/engine_service.h"
#include "example_app/ExampleApp/code/application/example_service.h"
#include "srp/example/ExampleDataStructure.h"
#include "srp/example/ExampleService/ExampleServiceHandler.h"
#include "srp/example/ExampleServiceSkeleton.h"

namespace srp {
namespace example {
ExampleApp::ExampleApp() {}
ExampleApp::~ExampleApp() {}

int ExampleApp::Initialize(
    const std::map<ara::core::StringView, ara::core::StringView> parms) {
  ara::log::LogInfo() << "App Initialized";
  return 0;
}

int ExampleApp::Run(const std::stop_token& token) {
  ara::log::LogInfo() << "App start";
  const ara::core::InstanceSpecifier diag_instance{
      "/srp/example/ExampleApp2/UDSReadVin"};
  const ara::core::InstanceSpecifier dtc_instance{
      "/srp/example/ExampleApp2/dtcMonitor1"};
  // Uslugi IPC/UDP (ExampleService) zostaja bez zmian; w trybie host/testy (SRP_EXAMPLE_NO_EM) pomijamy ja, bo UDP/SD
  // wymaga sieci z EM - E2E MAVLinka jej nie potrzebuje.
  std::unique_ptr<MyExampleService> serv;
  if (std::getenv("SRP_EXAMPLE_NO_EM") == nullptr) {
    serv = std::make_unique<MyExampleService>(
        ara::core::InstanceSpecifier{"srp/example/ExampleApp/service2"});
    serv->StartOffer();
  }

  // Kazde lacze z modelu (lora, wifi) bindujemy w kodzie: RegisterLinkTransport(nazwa, transport).
  // Tu: porty szeregowe z env (SerialTransport) albo własny IByteTransport. Kolejnosc: Initialize()
  // (RunAdaptiveLifecycle) -> RegisterLinkTransport() -> StartOffer()/uzycie proxy. Bez rejestracji lacze czeka.
  const char* lora_dev = std::getenv("SRP_EXAMPLE_LORA_DEVICE");
  if (lora_dev != nullptr) {
    const auto reg = ara::com::mavlink::RegisterLinkTransport(
        "lora", std::make_shared<ara::com::mavlink::SerialTransport>("lora", lora_dev, 57600));
    if (!reg.HasValue()) {
      ara::log::LogError() << "RegisterLinkTransport(lora) failed";
    }
  } else {
    ara::log::LogWarn() << "SRP_EXAMPLE_LORA_DEVICE not set: link lora waits for a transport";
  }
  const char* wifi_dev = std::getenv("SRP_EXAMPLE_WIFI_DEVICE");
  const auto wifi_reg = ara::com::mavlink::RegisterLinkTransport(
      "wifi", std::make_shared<ara::com::mavlink::SerialTransport>(
                  "wifi", wifi_dev != nullptr ? wifi_dev : "/tmp/srp_example/wifi_a", 115200));
  if (!wifi_reg.HasValue()) {
    ara::log::LogError() << "RegisterLinkTransport(wifi) failed";
  }
  MyEngineService engine_red{
      ara::core::InstanceSpecifier{"srp/example/ExampleApp/Engine_red"}, 2};
  MyEngineService engine_lora{
      ara::core::InstanceSpecifier{"srp/example/ExampleApp/Engine_lora"}, 3};
  MyEngineService engine_wifi{
      ara::core::InstanceSpecifier{"srp/example/ExampleApp/Engine_wifi"}, 4};
  engine_red.StartOffer();
  engine_lora.StartOffer();
  engine_wifi.StartOffer();

  uint32_t i = 0;
  uint32_t tick = 0;
  ara::log::LogInfo() << "App started";
  while (!token.stop_requested()) {
    core::condition::wait_for(std::chrono::milliseconds(10), token);
    ++tick;
    srp::example::EngineTelemetry t{static_cast<uint16_t>(tick), 21.5F};
    // Telemetry 100 Hz: limit instancji (max_rate_hz) decyduje, ile wyjdzie na radio (lora 1 Hz, wifi 20 Hz)
    engine_red.Telemetry.Update(t);
    engine_lora.Telemetry.Update(t);
    engine_wifi.Telemetry.Update(t);
    if (tick % 10 == 0) {  // CurrentMode 10 Hz
      const srp::example::EngineMode m{static_cast<uint8_t>(tick / 10)};
      engine_red.CurrentMode.Update(m);
      engine_lora.CurrentMode.Update(m);
      engine_wifi.CurrentMode.Update(m);
      engine_red.Trace.Update(1);  // "mavlink": false -> no-op na radiu (licznik tx_excluded)
    }
    if (tick % 10 == 0) {
      if (serv) {
        serv->Status.Update(10);
      }
    }
    ++i;
  }
  ara::log::LogInfo() << "App Stop";
  engine_red.StopOffer();
  engine_lora.StopOffer();
  engine_wifi.StopOffer();
  if (serv) {
    serv->StopOffer();
  }
  return 0;
}

}  // namespace example
}  // namespace srp
