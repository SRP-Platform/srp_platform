/**
 * @file dtc_service.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-12-07
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef APPS_COMMON_DIAG_DEMON_CODE_SERVICES_DTC_DTC_SERVICE_H_
#define APPS_COMMON_DIAG_DEMON_CODE_SERVICES_DTC_DTC_SERVICE_H_
#include <memory>
#include <vector>

#include "bindings/common/socket/proccess_socket.h"
#include "platform/log/logger.h"
#include "apps/common/diag_demon/code/services/dtc/i_dtc_service.h"
namespace srp {
namespace platform {
namespace diag_demon {
namespace dtc {
class DtcService : public IDtcService {
 private:
  const std::unique_ptr<srp::bindings::com::ProccessSocket> sock_;
  const platform::log::Logger& logger_;

  void RXCallback(const uint32_t peerid, const std::vector<uint8_t>& pyload);

 public:
  void Start() noexcept override;
  void Stop() noexcept override;

  DtcService(/* args */);
  ~DtcService() = default;
};
}  // namespace dtc
}  // namespace diag_demon
}  // namespace platform
}  // namespace srp

#endif  // APPS_COMMON_DIAG_DEMON_CODE_SERVICES_DTC_DTC_SERVICE_H_
