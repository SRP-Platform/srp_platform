/**
 * @file example_app.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-22
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "example/code/example_app.h"

#include "ara/log/log.h"
#include "core/common/condition.h"
#include "srp/example/ExampleServiceHandler.h"
namespace srp {
namespace example {
namespace {
const ara::core::InstanceSpecifier example_proxy_instance{
    "/srp/example/example_app_SWRoot/ExampleServiceRPort"};
}
int ExampleApp::Initialize(
    const std::map<ara::core::StringView, ara::core::StringView> parms) {
  ara::log::LogInfo() << "App started";
  return 0;
}

int ExampleApp::Run(const std::stop_token& token) {
  ara::log::LogInfo() << "App running";
  srp::example::ExampleServiceProxy proxy{example_proxy_instance};
  proxy.StartFindService([&proxy](auto) {
    proxy.StopFindService();
    ara::log::LogInfo() << "Proxy found";
  });
  core::condition::wait(token);
  proxy.StopFindService();
  ara::log::LogInfo() << "App stopped";

  return 0;
}

}  // namespace example
}  // namespace srp
