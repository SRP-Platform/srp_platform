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

#ifndef ARA_COM_MSG_TYPE_H_
#define ARA_COM_MSG_TYPE_H_
#include <cstdint>
#include <vector>

namespace ara {
namespace com {
enum class IpcMsgType : int8_t {
  kRequest = 0x01U,
  kResponse = 0x02U,
  kEvent = 0x03U,
  kSubscribe = 0x04U,
  kSubscribeAck = 0x05
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
};

}  // namespace com
}  // namespace ara
#endif  // ARA_COM_MSG_TYPE_H_
