/**
 * @file main.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-22
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "example/code/example_app.h"
#include "platform/exec/adaptive_lifecycle.h"
int main(int argc, char const *argv[]) {
  return platform::exec::RunAdaptiveLifecycle<srp::example::ExampleApp>(argc,
                                                                        argv);
}
