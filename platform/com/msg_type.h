/**
 * @file msg_type.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-22
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef PLATFORM_COM_MSG_TYPE_H_
#define PLATFORM_COM_MSG_TYPE_H_
#include <cstdint>
#include <vector>

namespace platform {
namespace com {
enum class IpcMsgType : uint8_t {
  kRequest = 0x01U,
  kResponse = 0x02U,
  kEvent = 0x03U,
  kSubscribe = 0x04U,
  kSubscribeAck = 0x05,
  kStopSubscribe = 0x06U,
  kRequestNoReturn = 0x07U,
  kRequestNoReturnAck = 0x08U,
  kRequestAck = 0x09U,
  kFind = 0x10U,
  kOffer = 0x11U,
  kError = 0xFFU,
};
struct IpcMsg {
  IpcMsgType msg_type_;
  uint16_t service_id_;
  uint16_t instance_id_;
  uint16_t method_id_;
  uint32_t app_id_;
  uint16_t session_id_;
  uint32_t length_;
  std::vector<uint8_t> payload_;

  bool operator==(const IpcMsg& other) const {
    bool flag = (msg_type_ == other.msg_type_);
    flag &= (service_id_ == other.service_id_);
    flag &= (instance_id_ == other.instance_id_);
    flag &= (method_id_ == other.method_id_);
    flag &= (app_id_ == other.app_id_);
    flag &= (session_id_ == other.session_id_);
    flag &= (length_ == other.length_);
    flag &= (payload_ == other.payload_);
    return flag;
  }
};

}  // namespace com
}  // namespace platform
#endif  // PLATFORM_COM_MSG_TYPE_H_
