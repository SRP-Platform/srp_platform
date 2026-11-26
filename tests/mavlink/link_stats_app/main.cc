/**
 * @file main.cc
 * @copyright Copyright (c) 2024
 */
#include "ara/exec/adaptive_lifecycle.h"
#include "tests/mavlink/link_stats_app/link_stats_app.h"

int main(int argc, char const* argv[]) {
  return ara::exec::RunAdaptiveLifecycle<srp::test::LinkStatsApp>(argc, argv);
}
