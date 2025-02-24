/**
 * @file example_app.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-22
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef EXAMPLE_CODE_EXAMPLE_APP_H_
#define EXAMPLE_CODE_EXAMPLE_APP_H_
#include <map>
#include <memory>
#include <string>

#include "ara/exec/adaptive_application.h"

namespace srp {
namespace example {
class ExampleApp final : public ara::exec::AdaptiveApplication {
 private:
  /**
   * @brief This function is called to initialiaze the application
   *
   * @param parms map with parms
   */
  int Initialize(const std::map<ara::core::StringView, ara::core::StringView>
                     parms) override;
  /**
   * @brief This function is called to launch the application
   *
   * @param token stop token
   */
  int Run(const std::stop_token& token) override;

 public:
  ExampleApp(/* args */) = default;
  ~ExampleApp() = default;
};
}  // namespace example
}  // namespace srp

#endif  // EXAMPLE_CODE_EXAMPLE_APP_H_
