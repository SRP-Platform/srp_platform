/**
 * Copyright (c) 2024
 *
 * @file main.cc
 */
#include "ara/exec/adaptive_lifecycle.h"
#include "platform/common/mavlink_daemon/code/application/mavlink_daemon.h"

int main(int argc, char const* argv[]) {
  return ara::exec::RunAdaptiveLifecycle<srp::mavlink_daemon::MavlinkDaemonApplication>(argc, argv);
}
