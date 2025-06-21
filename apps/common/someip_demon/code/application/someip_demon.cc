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
#include "apps/common/someip_demon/code/application/someip_demon.h"

#include <map>
#include <memory>
#include <memory_resource>
#include <string>


#include "core/common/condition.h"

namespace srp {
namespace someip_demon {

SomeIpApplication::SomeIpApplication(/* args */) {}

SomeIpApplication::~SomeIpApplication() {}

int SomeIpApplication::Run(const std::stop_token& token) {

  srp::core::condition::wait(token);

  return EXIT_SUCCESS;
}
/**
 * @brief This function is called to initialize the application
 *
 * @pappsm parms map with parms
 */
int SomeIpApplication::Initialize(
    const std::map<::platform::core::StringView, ::platform::core::StringView> parms) {

  return EXIT_SUCCESS;
}

}  // namespace someip_demon
}  // namespace srp
