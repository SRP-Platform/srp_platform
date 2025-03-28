/**
 * @file proxy.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-12
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef PLATFORM_COM_PROXY_PROXY_H_
#define PLATFORM_COM_PROXY_PROXY_H_

#include <functional>
#include <memory>
#include <utility>

#include "platform/com/com_error_domain.h"
#include "platform/com/internal/sd_component.h"
#include "platform/com/internal/sd_controller.h"
#include "platform/com/log.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/model_db.h"
#include "platform/core/result.h"

namespace platform {
namespace com {
template <typename T>
class Proxy {
 public:
  using Handler = std::unique_ptr<T>;
  using FindCallback = std::function<void(Handler)>;

 private:
  std::unique_ptr<platform::core::model::ModelCom> model_{nullptr};
  std::unique_ptr<platform::com::sd::SdProxyComponent> sd_{nullptr};
  FindCallback find_callback;

 public:
  explicit Proxy(const platform::core::InstanceSpecifier& instance) noexcept {
    auto model_opt =
        platform::core::ModelDataBase::GetInstance()
            .ResolveInstanceSpecifier<platform::core::model::ModelCom>(
                instance);
    if (!model_opt.HasValue()) {
      platform::com::LogFatal()
          << "Instance:" << instance.ToString() << " not found!";
      return;
    }
    platform::com::LogInfo() << "Model found: " << instance.ToString();
    model_ =
        std::make_unique<platform::core::model::ModelCom>(model_opt.Value());
    sd_ = platform::com::sd::SdController::StartFindServiceImpl(model_->type_);
  }

  explicit Proxy(Proxy&&) = delete;
  explicit Proxy(Proxy&) = delete;
  Proxy operator=(Proxy&) = delete;
  Proxy operator=(Proxy&&) = delete;

  ~Proxy() noexcept {}

  /**
   * @brief This method will trigger finding loop
   *
   * @pplatformm callback This callback will be call when Service will be found
   */
  platform::core::Result<void> StartFindService(
      const FindCallback&& callback) noexcept {
    find_callback = std::move(callback);
    if (model_ == nullptr) {
      return platform::com::MakeErrorCode(
          platform::com::ComErrc::kCommunicationStackError,
          "Model was nor resolved");
    }
    if (sd_ == nullptr) {
      return platform::com::MakeErrorCode(
          platform::com::ComErrc::kCommunicationStackError,
          "Service Discover already running!");
    }

    sd_->StartFindService(
        platform::com::sd::SdProxyComponent::SdEntry{
            model_->container_.service_model_.service_id_,
            model_->container_.service_model_.instance_id_, 1U, 0U},
        [this](auto com_wrapper) {
          auto tmp = std::make_unique<T>(model_->container_, com_wrapper);
          this->find_callback(std::move(tmp));
        });
    return {};
  }
  /**
   * @brief This method will stop find loop
   *
   */
  void StopFindService() noexcept {
    if (sd_) {
      sd_->Stop();
    }
  }
};
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_PROXY_PROXY_H_
