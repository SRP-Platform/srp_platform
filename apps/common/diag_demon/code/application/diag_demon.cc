
/**
 * @file diag_demon.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-01
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "apps/common/diag_demon/code/application/diag_demon.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "platform/log/log.h"
#include "core/common/condition.h"
#include "core/json/json_parser.h"
// #include "apps/common/diag_demon/code/services/dtc/dtc_service.h"
#include "apps/common/diag_demon/code/services/example_did.h"
#include "core/common/error_code.h"

namespace srp {
namespace platform {
namespace diag_demon {

/**
 * @brief This function is called to launch the application
 *
 * @pplatformm token stop token
 */
int DiagDemon::Run(const std::stop_token& token) {
  ::platform::log::LogInfo() << "Ecu id: " << std::to_string(ecu_id);
  ::platform::log::LogInfo() << "Eng token: " << std::to_string(eng_token);
  ::platform::log::LogInfo() << "Plant token: " << std::to_string(plant_token);
  ::platform::log::LogInfo() << "ECU MODE token: " << std::to_string(mode);
  if (eng_token == mode) {
    ::platform::log::LogInfo() << "ECU MODE: Eng mode";
  } else {
    ::platform::log::LogInfo() << "ECU MODE: Plant mode";
  }
  if (uds_server) {
    uds_server->Start();
  }
  core::condition::wait(token);
  if (uds_server) {
    uds_server->Stop();
  }
  return 0;
}
/**
 * @brief This function is called to initialiaze the application
 *
 * @pplatformm parms map with parms
 */
int DiagDemon::Initialize(
    const std::map<::platform::core::StringView, ::platform::core::StringView> parms) {
  auto obj_r = core::json::JsonParser::Parser(
      std::string{"/srp/opt/cpu_srp/diag_config.json"});
  if (!obj_r.has_value()) {
    ::platform::log::LogError() << "File not found: /srp/opt/cpu_srp/diag_config.json";
    return core::kError;
  }
  auto json_obj = obj_r.value();
  {
    auto ip_t = json_obj.GetNumber<uint32_t>("ecu_id");
    if (!ip_t.has_value()) {
      ::platform::log::LogError() << "ecu_id not found in config file";
      return core::kError;
    }
    ecu_id = ip_t.value();
  }
  {
    auto ip_t = json_obj.GetNumber<uint32_t>("eng_token");
    if (!ip_t.has_value()) {
      ::platform::log::LogError() << "eng_token not found in config file";
      return core::kError;
    }
    eng_token = ip_t.value();
  }
  {
    auto ip_t = json_obj.GetNumber<uint32_t>("plant-token");
    if (!ip_t.has_value()) {
      ::platform::log::LogError() << "plant-token not found in config file";
      return core::kError;
    }
    plant_token = ip_t.value();
  }
  {
    auto ip_t = json_obj.GetNumber<uint32_t>("current_mode");
    if (!ip_t.has_value()) {
      ::platform::log::LogError() << "current_mode not found in config file";
      return core::kError;
    }
    mode = ip_t.value();
  }

  {
    uint16_t logic_id{0};
    std::string vin{""};
    auto logic_id_t = json_obj.GetNumber<uint16_t>("logic_address");
    if (logic_id_t.has_value()) {
      logic_id = logic_id_t.value();
    }

    auto vin_t = json_obj.GetString("vin");
    if (vin_t.has_value()) {
      vin = vin_t.value();
      ::platform::log::LogInfo() << "VIN: " << vin;
    }

    if ((logic_id != 0x0000) && vin.length() > 0 && vin.length() < 18) {
      uds_server = std::make_unique<uds::UdsServer>(logic_id, vin);
    } else {
      ::platform::log::LogError() << "Wrong parms for UDS";
    }
    // dtc_service_ = std::make_unique<dtc::DtcService>();
  }
  return 0;
}
}  // namespace diag_demon
}  // namespace platform
}  // namespace srp
