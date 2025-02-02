/**
 * @file routable_uds_service.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-30
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_DIAG_ROUTING_ROUTABLE_UDS_SERVICE_H_
#define PLATFORM_DIAG_ROUTING_ROUTABLE_UDS_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "bindings/common/socket/Isocket_stream.h"
#include "bindings/common/socket/stream_ipc_socket.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/result.h"
#include "platform/log/logger.h"

namespace platform {
namespace diag {
/// @brief Positive response of a handled UDS request
struct OperationOutput {
  /// @brief Response byte array
  std::vector<uint8_t> responseData;
};

/// @brief UDS services routing namespace
/// @note The namespace is not part of the platform standard
namespace routing {
/// @brief An UDS service to handle a diagnostic request by offering itself to a
/// router
class RoutableUdsService {
 private:
  const uint8_t mSid;
  bool mOffered;
  const platform::core::InstanceSpecifier mSpecifier;
  const platform::log::Logger &diag_logger_;
  std::string job_id_{""};
  const std::unique_ptr<srp::bindings::com::soc::StreamIpcSocket> sock_;

 protected:
  /// @brief SID increment for a positive response
  static const uint8_t cPositiveResponseSidIncrement{0x40};
  /// @brief Unsupported sub-function NRC
  const uint8_t cSubFunctionNotSupported{0x12};
  /// @brief Incorrect message length NRC
  const uint8_t cIncorrectMessageLength{0x13};
  /// @brief Incorrect request sequence NRC
  const uint8_t cRequestSequenceError{0x24};

  /// @brief Constructor
  /// @pplatformm specifier Owner instance specifier
  /// @pplatformm sid UDS service ID
  RoutableUdsService(
      const platform::core::InstanceSpecifier &specifier, uint8_t sid,
      std::unique_ptr<srp::bindings::com::soc::StreamIpcSocket> sock =
          std::make_unique<srp::bindings::com::soc::StreamIpcSocket>()) noexcept;

  /// @brief Generate a negative response with a specific NRC
  /// @pplatformm[out] response Generated negative response
  /// @pplatformm nrc Given Negative Response Code (NRC)
  void GenerateNegativeResponse(OperationOutput &response,  // NOLINT
                                uint8_t nrc) const;
  std::vector<uint8_t> RXLoop(const std::vector<uint8_t> &data);

 public:
  /// @brief Negative response code service ID
  static const uint8_t cNegativeResponseCodeSid{0x7f};

  /// @brief Get SID
  /// @returns Service identification
  uint8_t GetSid() const noexcept;

  /// @brief Offer handling DM requests
  /// @returns Error result if the service has been already offered
  platform::core::Result<void> Offer();

  /// @brief Get offering status of the service
  /// @returns True if the service has been offered, otherwise false
  bool IsOffered() const noexcept;

  /// @brief Handle an UDS request message
  /// @pplatformm requestData Request message byte array
  /// @pplatformm metaInfo Request message metainfo
  /// @pplatformm cancellationHandler Callback to be invoked when the current
  /// conversation is cancelled
  /// @returns Response byte array
  virtual platform::core::Result<OperationOutput> HandleMessage(
      const std::vector<uint8_t> &requestData) = 0;

  /// @brief Stop offering request handling
  void StopOffer() noexcept;

  virtual ~RoutableUdsService() noexcept = default;
};
}  // namespace routing
}  // namespace diag
}  // namespace platform

#endif  // PLATFORM_DIAG_ROUTING_ROUTABLE_UDS_SERVICE_H_
