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

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <utility>
#include <vector>

#include "ara/com/mavlink/link_transport_registry.h"
#include "ara/com/mavlink/serial_transport.h"
#include "ara/log/log.h"
#include "core/common/condition.h"
#include "srp/example/ExampleDataStructure.h"
#include "srp/example/EngineService/EngineServiceHandler.h"
#include "srp/example/ExampleService/ExampleServiceHandler.h"

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
                  "wifi", wifi_dev != nullptr ? wifi_dev : "/tmp/srp_example/wifi_b", 115200));
  if (!wifi_reg.HasValue()) {
    ara::log::LogError() << "RegisterLinkTransport(wifi) failed";
  }

  // Trzy proxy EngineService: redundancja (2), tylko lora (3), tylko wifi (4) - patrz example_app.json.
  struct Client {
    int id;
    std::unique_ptr<srp::example::EngineServiceProxy> proxy;
    std::shared_ptr<srp::example::EngineServiceHandler> handler;
  };
  std::mutex mutex;
  std::vector<std::unique_ptr<Client>> clients;
  const std::vector<std::pair<int, const char*>> specs{
      {2, "srp/example/ExampleApp2/Engine_red_in"},
      {3, "srp/example/ExampleApp2/Engine_lora_in"},
      {4, "srp/example/ExampleApp2/Engine_wifi_in"}};
  for (const auto& spec : specs) {
    auto c = std::make_unique<Client>();
    c->id = spec.first;
    c->proxy = std::make_unique<srp::example::EngineServiceProxy>(
        ara::core::InstanceSpecifier{spec.second});
    Client* cp = c.get();
    c->proxy->StartFindService([cp, &mutex](auto handler) {
      std::lock_guard<std::mutex> lock{mutex};
      cp->handler = std::move(handler);
      auto h = cp->handler;
      const int id = cp->id;
      h->CurrentMode.Subscribe(1, [h, id](auto) {
        h->CurrentMode.SetReceiveHandler([h, id]() {
          const auto v = h->CurrentMode.GetNewSamples();
          if (v.HasValue()) {
            std::cout << "EVT CurrentMode instance=" << id << " mode=" << static_cast<int>(v.Value().mode) << std::endl;
          }
        });
      });
      h->Telemetry.Subscribe(1, [h, id](auto) {
        h->Telemetry.SetReceiveHandler([h, id]() {
          const auto v = h->Telemetry.GetNewSamples();
          if (v.HasValue()) {
            std::cout << "EVT Telemetry instance=" << id << " rpm=" << v.Value().rpm << std::endl;
          }
        });
      });
    });
    clients.push_back(std::move(c));
  }

  uint32_t tick = 0;
  ara::log::LogInfo() << "App started";
  while (!token.stop_requested()) {
    core::condition::wait_for(std::chrono::milliseconds(500), token);
    ++tick;
    std::lock_guard<std::mutex> lock{mutex};
    for (const auto& c : clients) {
      if (!c->handler) {
        continue;
      }
      const auto r = c->handler->SetMode(srp::example::EngineMode{static_cast<uint8_t>(tick)});
      if (r.HasValue()) {
        std::cout << "CALL SetMode instance=" << c->id << " ok mode=" << static_cast<int>(r.Value().mode) << std::endl;
      } else {
        std::cout << "CALL SetMode instance=" << c->id << " err=" << r.Error().Value() << std::endl;
      }
      const auto d = c->handler->DebugDump();
      std::cout << "CALL DebugDump instance=" << c->id << (d.HasValue() ? " ok" : " err=")
                << (d.HasValue() ? 0 : d.Error().Value()) << std::endl;
    }
  }
  ara::log::LogInfo() << "App Stop";
  return 0;
}

}  // namespace example
}  // namespace srp
