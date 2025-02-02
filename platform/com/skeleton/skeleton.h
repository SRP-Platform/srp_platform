/**
 * @file skeleton.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-03-28
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef PLATFORM_COM_SKELETON_SKELETON_H_
#define PLATFORM_COM_SKELETON_SKELETON_H_

#include <memory>

#include "platform/com/com_error_domain.h"
#include "platform/com/instance_identifier_container.h"
#include "platform/com/internal/sd_component.h"
#include "platform/com/internal/sd_controller.h"
#include "platform/com/log.h"
#include "platform/com/skeleton/i_skeleton_com_wrapper.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/model_db.h"
#include "platform/core/result.h"

namespace platform {
namespace com {
namespace skeleton {
template <class Interface>
class Skeleton : public Interface {
 public:
  using InterfaceType = Interface;

 private:
  Skeleton(const platform::com::InstanceIdentifierContainer container,
           std::shared_ptr<wrapper::ISkeletonComWrapper> com_wrapper)
      : Interface(container, com_wrapper) {}

 public:
  static platform::core::Result<std::shared_ptr<Skeleton<Interface>>> Create(
      const platform::core::InstanceSpecifier& instance) noexcept {
    auto model_opt =
        platform::core::ModelDataBase::GetInstance()
            .ResolveInstanceSpecifier<platform::core::model::ModelCom>(
                instance);
    if (!model_opt.HasValue()) {
      platform::com::LogFatal()
          << "Instance:" << instance.ToString() << " not found!";
      return platform::com::MakeErrorCode(
          platform::com::ComErrc::kInstanceIDCouldNotBeResolved, "");
    }
    platform::com::LogInfo() << "Model found: " << instance.ToString();
    auto model = model_opt.Value();
    auto com_wrapper = sd::SdController::StartOfferServiceImpl(
        model.type_, model.container_.service_model_.service_id_,
        model.container_.service_model_.instance_id_, 0x00, 0x01);
    if (com_wrapper) {
      auto* t = new Skeleton<Interface>{model.container_, com_wrapper};
      return std::shared_ptr<Skeleton<Interface>>(t);
    }
    return platform::com::MakeErrorCode(
        platform::com::ComErrc::kCommunicationStackError, "");
  }

  platform::core::Result<void> OfferService() noexcept {
    if (!this->com_wrapper_) {
      return platform::com::MakeErrorCode(
          platform::com::ComErrc::kCommunicationLinkError, "");
    }
    return this->com_wrapper_->Start();
  }

  void StopOfferService() {
    if (this->com_wrapper_) {
      this->com_wrapper_->Release();
    }
  }
};
}  // namespace skeleton
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_SKELETON_SKELETON_H_
