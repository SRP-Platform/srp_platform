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
#ifndef ARA_COM_PROXY_PROXY_H_
#define ARA_COM_PROXY_PROXY_H_
#include <functional>
#include <memory>

#include "ara/com/com_error_domain.h"
#include "ara/com/internal/sd_component.h"
#include "ara/com/log.h"
#include "ara/core/instance_specifier.h"
#include "ara/core/model_db.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
template <typename T>
class Proxy {
 public:
  using Handler = std::shared_ptr<T>;
  using FindCallback = std::function<void(Handler)>;

 private:
  Handler handler_ptr_{nullptr};
  std::unique_ptr<ara::core::model::ModelCom> model_{nullptr};
  std::unique_ptr<ara::com::sd::SdComponent> sd_{nullptr};

 public:
  explicit Proxy(const ara::core::InstanceSpecifier& instance) noexcept {
    auto model_opt =
        ara::core::ModelDataBase::GetInstance()
            .ResolveInstanceSpecifier<ara::core::model::ModelCom>(instance);
    if (!model_opt.HasValue()) {
      ara::com::LogFatal() << "Instance:" << instance.ToString()
                           << " not found!";
    }
    ara::com::LogDebug() << "Model found: " << instance.ToString();
    model_ = std::make_unique<ara::core::model::ModelCom>(model_opt.Value());
  }

  explicit Proxy(Proxy&&) = delete;
  explicit Proxy(Proxy&) = delete;
  Proxy operator=(Proxy&) = delete;
  Proxy operator=(Proxy&&) = delete;

  ~Proxy() noexcept {}

  /**
   * @brief This method will trigger finding loop
   *
   * @param callback This callback will be call when Service will be found
   */
  ara::core::Result<void> StartFindService(
      const FindCallback&& callback) noexcept {
    if (model_ == nullptr) {
      return ara::com::MakeErrorCode(ara::com::ComErrc::kUnsetFailure,
                                     "Model was nor resolved");
    }
    if (sd_ != nullptr) {
      return ara::com::MakeErrorCode(ara::com::ComErrc::kUnsetFailure,
                                     "Service Discover already running!");
    }

    sd_->StartFindService(
        ara::com::sd::SdComponent::SdEntry{
            model_->container_.service_model_.service_id_,
            model_->container_.service_model_.instance_id_, 1U, 0U},
        [](auto&) {});
    return;
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
}  // namespace ara

#endif  // ARA_COM_PROXY_PROXY_H_
