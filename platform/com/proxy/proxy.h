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

#include "platform/com/com_error_domain.h"
#include "platform/com/internal/sd_component.h"
#include "platform/com/log.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/model_db.h"
#include "platform/core/result.h"

namespace platform {
namespace com {
template <typename T>
class Proxy {
 public:
  using Handler = std::shared_ptr<T>;
  using FindCallback = std::function<void(Handler)>;

 private:
  Handler handler_ptr_{nullptr};
  std::unique_ptr<platform::core::model::ModelCom> model_{nullptr};
  std::unique_ptr<platform::com::sd::SdComponent> sd_{nullptr};

 public:
  explicit Proxy(const platform::core::InstanceSpecifier& instance) noexcept {
    auto model_opt =
        platform::core::ModelDataBase::GetInstance()
            .ResolveInstanceSpecifier<platform::core::model::ModelCom>(instance);
    if (!model_opt.HasValue()) {
      platform::com::LogFatal() << "Instance:" << instance.ToString()
                           << " not found!";
    }
    platform::com::LogDebug() << "Model found: " << instance.ToString();
    model_ = std::make_unique<platform::core::model::ModelCom>(model_opt.Value());
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
    if (model_ == nullptr) {
      return platform::com::MakeErrorCode(platform::com::ComErrc::kUnsetFailure,
                                     "Model was nor resolved");
    }
    if (sd_ != nullptr) {
      return platform::com::MakeErrorCode(platform::com::ComErrc::kUnsetFailure,
                                     "Service Discover already running!");
    }

    sd_->StartFindService(
        platform::com::sd::SdComponent::SdEntry{
            model_->container_.service_model_.service_id_,
            model_->container_.service_model_.instance_id_, 1U, 0U},
        [](auto&) {});
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

  Handler GetActualProxy() noexcept { return handler_ptr_; }
};
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_PROXY_PROXY_H_
