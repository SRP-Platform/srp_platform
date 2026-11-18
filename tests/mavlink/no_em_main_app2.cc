/**
 * @file no_em_main_app2.cc
 * @copyright Copyright (c) 2024
 * @brief Test main of example_app2: runs the application without Execution Management, which
 * does not exist on the host that runs the end-to-end tests.
 */
#include "ara/exec/adaptive_lifecycle.h"
#include "example_app2/ExampleApp2/code/application/ExampleApp.h"

int main(int argc, char const *argv[]) {
  return ara::exec::RunAdaptiveLifecycleWithoutControl<srp::example::ExampleApp>(argc, argv);
}
