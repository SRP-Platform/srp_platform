/**
 * @file state_manager.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-12-30
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "apps/common/state_manager/code/state_manager.h"

#include <string_view>

#include "core/common/condition.h"
#include "core/json/json_parser.h"
#include "apps/common/state_manager/code/com_wrapper.h"
#include "platform/exec/sm/initialization.h"
#include "platform/log/log.h"
#include "srp/platform/sm/StateManager/StateManagerHandler.h"

namespace srp {
namespace sm {
namespace {
static constexpr std::string_view kConfigPath{
    "/srp/opt/cpu_srp/machine_states.json"};
static const platform::core::InstanceSpecifier did_instance_{
    "/srp/platform/state_manager/CurrentMachineStatePPort"};
}  // namespace

StateManager::StateManager(/* args */) {}

StateManager::~StateManager() {}

int StateManager::Initialize(
    const std::map<platform::core::StringView, platform::core::StringView>
        parms) {
  const auto& file =
      srp::core::json::JsonParser::Parser(std::string{kConfigPath});
  if (!file.has_value()) {
    platform::log::LogError() << "Missing config file!";
    return -1;
  }
  const auto& json = file.value();
  const auto machine_state = json.GetArray<std::string>("machine_state");
  if (!machine_state.has_value()) {
    platform::log::LogError() << "Incorrect json structure!";
    return -2;
  }
  platform::log::LogInfo() << "App Initialized";
  return 0;
}

int StateManager::Run(const std::stop_token& token) {
  srp::platform::sm::StateManagerProxy sm_proxy{
      platform::core::InstanceSpecifier{
          "srp/platform/state_manager/SmServiceRPort"}};
  sm_proxy.StartFindService(
      [this](srp::platform::sm::StateManagerProxy::handler_t handler) {
        platform::log::LogDebug() << "Service Founded";

        handler->CurrentState.Subscribe(1, [this, handler](const auto) {
          handler->CurrentState.SetReceiveHandler([this, handler]() {
            const auto val = handler->CurrentState.GetNewSamples();
            if (val.HasValue()) {
              platform::log::LogDebug() << "Event Value: " << val.Value();
              this->state_con_->StatusUpdateEvent({val.Value(), 0});
            }
          });
        });

        this->state_con_ = platform::exec::sm::Initialize(
            std::make_unique<ComWrapper>(handler));
        this->state_con_->Init();
        this->did_ = std::make_unique<DiDImpl>(did_instance_, this->state_con_);
        this->did_->Offer();
      });
  platform::log::LogInfo() << "App Started";
  srp::core::condition::wait(token);
  sm_proxy.StopFindService();
  if (did_) {
    did_->StopOffer();
  }
  platform::log::LogInfo() << "App Stoping";
  return 0;
}

}  // namespace sm
}  // namespace srp
