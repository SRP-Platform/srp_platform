/**
 * @file ipc_service_discover_proxy.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-08
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef BINDINGS_COM_IPC_PROXY_IPC_SERVICE_DISCOVER_PROXY_H_
#define BINDINGS_COM_IPC_PROXY_IPC_SERVICE_DISCOVER_PROXY_H_

#include <filesystem>
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <utility>

#include "bindings/com/ipc/ipc_controller.h"
#include "bindings/com/ipc/proxy/ipc_proxy.h"
#include "bindings/com/sd_bindings.h"
#include "bindings/log.h"
#include "core/common/condition.h"

namespace srp {
namespace bindings {
namespace com {
namespace proxy {
class IpcServiceDiscover_Proxy
    : public ::srp::bindings::com::SdComponentInterface {
 private:
  std::unique_ptr<std::jthread> loop_thread_;
  std::string name{};
  ::srp::bindings::com::SdComponentInterface::SDFindCallback callback_;

 public:
  IpcServiceDiscover_Proxy(/* args */) = default;
  ~IpcServiceDiscover_Proxy() = default;
  void StartFindService(
      const ::srp::bindings::com::SdComponentInterface::SdEntry& entry,
      ::srp::bindings::com::SdComponentInterface::SDFindCallback&&
          callback) noexcept {
    if (loop_thread_) {
      srp::bindings::LogError() << "[IPC]: SD loop already started";
      return;
    }
    callback_ = std::move(callback);
    name = "s-" + std::to_string(static_cast<uint64_t>(
                      (static_cast<uint64_t>(entry.service_id_) << 32) +
                      entry.instance_id_));

    loop_thread_ =
        std::make_unique<std::jthread>([this](std::stop_token stop_token) {
          srp::bindings::LogInfo()
              << "[IPC]: SD loop starting for: " << this->name;
          while (!stop_token.stop_requested()) {
            if (std::filesystem::exists("/run/" + this->name)) {
              srp::bindings::LogInfo()
                  << "[IPC]: Proxy Found -> " << "/run/" << this->name;
              callback_(bindings::com::ipc::IpcController::GetInstance()
                            ->CreateHandler<bindings::com::proxy::IpcProxy>(
                                this->name));
              break;
            }
            srp::core::condition::wait_for(std::chrono::milliseconds{200},
                                           stop_token);
          }
          srp::core::condition::wait(stop_token);
          srp::bindings::LogInfo() << "[IPC]: SD loop stop";
        });
  }

  void Stop() noexcept {
    if (loop_thread_) {
      loop_thread_->request_stop();
      loop_thread_->join();
      loop_thread_.reset();
      callback_ = nullptr;
    }
  }
};
}  // namespace proxy
}  // namespace com
}  // namespace bindings
}  // namespace srp
#endif  // BINDINGS_COM_IPC_PROXY_IPC_SERVICE_DISCOVER_PROXY_H_
