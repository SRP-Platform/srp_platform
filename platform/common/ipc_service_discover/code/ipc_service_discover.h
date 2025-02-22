/**
 * @file ipc_service_discover.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-21
 *
 * @copyright Copyright (c) 2025
 *
 */

/**
 * @file dlt_service.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-02-07
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_COMMON_CODE_IPC_SERVICE_DISCOVER_H_
#define PLATFORM_COMMON_CODE_IPC_SERVICE_DISCOVER_H_
#include <map>
#include <memory>
#include <string>

#include "ara/exec/adaptive_application.h"
#include "bindings/common/socket/proccess_socket.h"

namespace srp {
namespace platform {
namespace com {
namespace ipc {
class IpcServiceDiscover final : public ara::exec::AdaptiveApplication {
 private:
  const std::unique_ptr<srp::bindings::com::ProccessSocket> sock_;
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
  void ProceedWithMsg(const uint32_t pid, const std::vector<uint8_t>& data);

 public:
  IpcServiceDiscover(/* args */);
  ~IpcServiceDiscover();
};
}  // namespace ipc
}  // namespace com
}  // namespace platform
}  // namespace srp
#endif  // PLATFORM_COMMON_CODE_IPC_SERVICE_DISCOVER_H_
