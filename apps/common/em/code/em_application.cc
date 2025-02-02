/**
 * @file em_application.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-02-06
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "apps/common/em/code/em_application.h"

#include <map>
#include <string>

#include "apps/common/em/code/services/em/app_db.h"
#include "core/common/condition.h"
#include "core/json/json_parser.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/result.h"
#include "platform/log/log.h"

namespace srp {
namespace em {
namespace {
static const ::platform::core::InstanceSpecifier kSmServiceInstance{
    "/srp/platform/em_SWRoot/StateManagerPPort"};
}  // namespace

EmApplication::EmApplication(/* args */) {}

EmApplication::~EmApplication() {}

int EmApplication::Run(const std::stop_token& token) {
  this->sm_service_->OfferService();
  // this->em_service->SetActiveState(46617);
  while (!token.stop_requested()) {
    const auto val = this->cmd_list_.Get(token);
    if (val.has_value()) {
      this->em_service->SetActiveState(val.value());
    }
  }
  this->sm_service_->StopOfferService();
  return 0;
}
/**
 * @brief This function is called to initialize the application
 *
 * @pplatformm parms map with parms
 */
int EmApplication::Initialize(
    const std::map<::platform::core::StringView, ::platform::core::StringView>
        parms) {
  this->sm_service_ =
      srp::platform::sm::StateManagerSkeleton::Create(kSmServiceInstance)
          .Value();
  this->sm_service_->SetState.Bind([this](uint16_t new_state) {
    ::platform::log::LogInfo() << "State Update: " << new_state;
    this->cmd_list_.push(new_state);
    return ::platform::core::Result<void>{};
  });
  this->sm_service_->GetState.Bind(
      [this]() { return this->em_service->GetActiveState(); });

  const auto db = std::make_shared<service::data::AppDb>();
  em_service =
      std::make_shared<service::EmService>(db, [this](const uint16_t& new_id) {
        if (this->sm_service_) {
          ::platform::log::LogError() << "Event update";
          this->sm_service_->CurrentState.Send(new_id);
        }
      });

  const auto json_opt = srp::core::json::JsonParser::Parser(
      std::string{"/srp/platform/etc/state_config.json"});
  if (!json_opt.has_value()) {
    return -1;
  }
  const srp::core::json::JsonParser json = json_opt.value();

  const auto states_opt = json.GetArray<nlohmann::json>("states");
  if (!states_opt.has_value()) {
    ::platform::log::LogError() << "Incorrect states type";
    return -1;
  }
  const auto states = states_opt.value();
  for (const auto& item : states) {
    srp::core::json::JsonParser state =
        srp::core::json::JsonParser::Parser(item).value();
    std::string name = state.GetString("name").value();
    uint16_t id = state.GetNumber<uint16_t>("id").value();
    ::platform::log::LogInfo() << "New state: " << name << "[" << id << "]";
    db->InsertNewFG(id, name);
  }
  this->em_service->LoadApps();
  return 0;
}

}  // namespace em
}  // namespace srp
