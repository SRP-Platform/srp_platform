/**
 * @file i_proxy_com_wrapper.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-23
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef ARA_COM_PROXY_I_PROXY_COM_WRAPPER_H_
#define ARA_COM_PROXY_I_PROXY_COM_WRAPPER_H_
#include <functional>

#include "ara/com/msg_type.h"

namespace ara {
namespace com {
namespace proxy {
namespace wrapper {
class IProxyComWrapper {
 public:
 using ProceedFrameCallback = std::function<void(const ara::com::IpcMsg&&)>;
  virtual void ProceedFrame(const ara::com::IpcMsg& msg) noexcept = 0;
  virtual void TransmitFrame(const ara::com::IpcMsg& msg) noexcept = 0;
  virtual void SetProceedFrameCallback(const ProceedFrameCallback&& callback) noexcept = 0;
  virtual ~IProxyComWrapper() = default;
};
}  // namespace wrapper
}  // namespace proxy
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_PROXY_I_PROXY_COM_WRAPPER_H_
