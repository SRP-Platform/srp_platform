/**
 * @file ipc_proxy.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "bindings/com/ipc/proxy/ipc_proxy.h"

#include <utility>

#include "bindings/com/ipc/com_thread_pool.h"
#include "bindings/log.h"

namespace srp {
namespace bindings {
namespace com {
namespace proxy {

IpcProxy::IpcProxy(const uint16_t& session_id, const std::string& desc)
    : srp::bindings::com::ipc::Handler{session_id},
      desc_{desc},
      thread_pool_{} {}

void IpcProxy::ProceedFrame(const platform::com::IpcMsg& msg) noexcept {
  srp::bindings::LogInfo() << "[Proxy] " << "New Msg. msg type: "
                           << static_cast<uint>(msg.msg_type_);
  ComThreadPool::GetInstance().Post([this, msg]() {
    this->::platform::com::wrapper::ComWrapper::callback_(std::move(msg));
  });
  srp::bindings::LogInfo() << "[Proxy] " << "callback_ OUT";
}

bool IpcProxy::TransmitFrame(const platform::com::IpcMsg& msg) noexcept {
  platform::com::IpcMsg out_msg{msg};
  out_msg.session_id_ = this->session_id_;

  return this->srp::bindings::com::ipc::Handler::callback_(desc_,
                                                           std::move(out_msg));
}
void IpcProxy::Release() {}
}  // namespace proxy
}  // namespace com
}  // namespace bindings
}  // namespace srp
