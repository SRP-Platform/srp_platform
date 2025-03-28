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

#include "core/common/condition.h"
#include "platform/log/log.h"
#include "srp/example/ExampleServiceHandler.h"
namespace srp {
namespace example {
namespace {
const platform::core::InstanceSpecifier example_proxy_instance{
    "/srp/example/example_app_SWRoot/ExampleServiceRPort"};
}
int ExampleApp::Initialize(
    const std::map<platform::core::StringView, platform::core::StringView>
        parms) {
  platform::log::LogInfo() << "App started";
  return 0;
}

int ExampleApp::Run(const std::stop_token& token) {
  platform::log::LogInfo() << "App running";
  srp::example::ExampleServiceProxy proxy{example_proxy_instance};
  // proxy.StartFindService([&proxy](auto) {
  //   proxy.StopFindService();
  //   platform::log::LogInfo() << "Proxy found";
  // });
  while (true) {
    core::condition::wait_for(std::chrono::seconds{1}, token);
    // platform::log::LogInfo() << "1";
  }

  proxy.StopFindService();
  platform::log::LogInfo() << "App stopped";

  return 0;
}

}  // namespace example
}  // namespace srp
