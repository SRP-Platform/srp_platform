/**
 * @file ExampleApp2.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-27
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef EXAMPLE_APP2_EXAMPLEAPP2_CODE_APPLICATION_EXAMPLEAPP_H_
#define EXAMPLE_APP2_EXAMPLEAPP2_CODE_APPLICATION_EXAMPLEAPP_H_

#include <map>
#include <memory>

#include "ara/exec/adaptive_application.h"
#include "srp/example/ExampleService/ExampleServiceHandler.h"

namespace srp {
namespace example {
class ExampleApp : public ara::exec::AdaptiveApplication {
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
  ExampleApp();
  ~ExampleApp();

 private:
  std::shared_ptr<srp::example::ExampleServiceHandler> handler_;
};
}  // namespace example
}  // namespace srp
#endif  // EXAMPLE_APP2_EXAMPLEAPP2_CODE_APPLICATION_EXAMPLEAPP_H_
