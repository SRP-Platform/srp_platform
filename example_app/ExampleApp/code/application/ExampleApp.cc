/**
 * @file ExampleApp.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-27
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "example_app/ExampleApp/code/application/ExampleApp.h"

#include <iostream>

#include "ara/log/log.h"
#include "core/common/condition.h"
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
      "/srp/example/ExampleApp/UDSReadVin"};
  const ara::core::InstanceSpecifier dtc_instance{
      "/srp/example/ExampleApp/dtcMonitor1"};
  MyExampleService serv{
      ara::core::InstanceSpecifier{"srp/example/ExampleApp/service2"}};

  serv.StartOffer();

  ara::log::LogInfo() << "App started";
  while (!token.stop_requested()) {
    core::condition::wait_for(std::chrono::milliseconds(100), token);
    serv.Status.Update(10);
  }
  ara::log::LogInfo() << "App Stop";
  serv.StopOffer();
  return 0;
}

}  // namespace example
}  // namespace srp
