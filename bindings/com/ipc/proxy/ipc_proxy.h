/**
 * @file ipc_proxy.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef BINDINGS_COM_IPC_PROXY_IPC_PROXY_H_
#define BINDINGS_COM_IPC_PROXY_IPC_PROXY_H_

#include <boost/asio/thread_pool.hpp>  // NOLINT
#include <string>                      // NOLINT

#include "bindings/com/ipc/handler.h"
#include "platform/com/com_wrapper.h"

namespace srp {
namespace bindings {
namespace com {
namespace proxy {
class IpcProxy : public srp::bindings::com::ipc::Handler,
                 public platform::com::wrapper::ComWrapper {
 private:
  const std::string desc_;
  boost::asio::thread_pool thread_pool_;

 public:
  void ProceedFrame(const platform::com::IpcMsg& msg) noexcept override;
  bool TransmitFrame(const platform::com::IpcMsg& msg) noexcept override;
  void Release() override;
  IpcProxy(const uint16_t& session_id, const std::string& desc);
  virtual ~IpcProxy() = default;
};

}  // namespace proxy
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COM_IPC_PROXY_IPC_PROXY_H_
