/**
 * @file main.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-21
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "apps/common/ipc_service_discover/code/ipc_service_discover.h"
#include "platform/exec/adaptive_lifecycle.h"

int main(int argc, char const *argv[]) {
  return platform::exec::RunAdaptiveLifecycle<
      srp::apps::com::ipc::IpcServiceDiscover>(argc, argv);
}
