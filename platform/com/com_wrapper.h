/**
 * @file com_wrapper.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef PLATFORM_COM_COM_WRAPPER_H_
#define PLATFORM_COM_COM_WRAPPER_H_

#include <functional>
#include <utility>

#include "platform/com/msg_type.h"
#include "platform/core/result.h"

namespace platform {
namespace com {
namespace wrapper {
class ComWrapper {
 public:
  using ProceedFrameCallback =
      std::function<void(const platform::com::IpcMsg&&)>;

 protected:
  ProceedFrameCallback callback_;

 public:
  virtual void ProceedFrame(const platform::com::IpcMsg& msg) noexcept = 0;
  virtual bool TransmitFrame(const platform::com::IpcMsg& msg) noexcept = 0;
  virtual void SetProceedFrameCallback(
      const ProceedFrameCallback&& callback) noexcept {
    callback_ = std::move(callback);
  }
  virtual void Release() {}
  virtual platform::core::Result<void> Start() { return {}; }
  virtual ~ComWrapper() = default;
};
}  // namespace wrapper
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_COM_WRAPPER_H_
