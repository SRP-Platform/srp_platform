/**
 * @file main.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-27
 *
 * @copyright Copyright (c) 2024
 *
 */
#include <cstdlib>
#include <thread>  // NOLINT

#include "ara/exec/adaptive_lifecycle.h"
#include "example_app2/ExampleApp2/code/application/ExampleApp.h"

int main(int argc, char const *argv[]) {
  // SRP_EXAMPLE_NO_EM=1: uruchomienie bez Execution Management (host, testy E2E)
  const bool no_em = std::getenv("SRP_EXAMPLE_NO_EM") != nullptr;
  std::this_thread::sleep_for(std::chrono::seconds{no_em ? 0 : 2});
  if (no_em) {
    return ara::exec::RunAdaptiveLifecycleWithoutControl<srp::example::ExampleApp>(argc, argv);
  }
  return ara::exec::RunAdaptiveLifecycle<srp::example::ExampleApp>(argc,
                                                                     argv);
}
