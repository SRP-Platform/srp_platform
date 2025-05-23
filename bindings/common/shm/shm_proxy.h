/**
 * @file shm_proxy.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-11
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef BINDINGS_COMMON_SHM_SHM_PROXY_H_
#define BINDINGS_COMMON_SHM_SHM_PROXY_H_

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "platform/com/com_error_domain.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/result.h"

namespace srp {
namespace bindings {
namespace com {
namespace shm {
template <typename shm_type_t>
class ShmProxy final {
 private:
  const platform::core::InstanceSpecifier instance_specifier_;
  int shm_des{0};
  const int mode = S_IRWXU | S_IRWXG;
  struct shm_handler_t {
    pthread_mutex_t mutex_;
    shm_type_t data;
  };

  shm_handler_t* handler = nullptr;

 public:
  explicit ShmProxy(const platform::core::InstanceSpecifier& instance_specifier)
      : instance_specifier_{instance_specifier} {}

  ShmProxy(ShmProxy&) = delete;
  ShmProxy(ShmProxy&&) = delete;
  ShmProxy& operator=(ShmProxy&) = delete;
  ShmProxy& operator=(ShmProxy&&) = delete;

  platform::core::Result<void> FindService() noexcept {
    shm_des = shm_open(instance_specifier_.ToString().c_str(), O_RDWR, mode);
    if (shm_des <= 0) {
      return MakeErrorCode(platform::com::ComErrc::kNetworkBindingFailure, "");
    }
    if (ftruncate(shm_des, sizeof(shm_handler_t)) == -1) {
      return MakeErrorCode(platform::com::ComErrc::kFieldValueIsNotValid, "");
    }
    handler = reinterpret_cast<shm_handler_t*>(mmap(NULL, sizeof(shm_handler_t),
                                                    PROT_READ | PROT_WRITE,
                                                    MAP_SHARED, shm_des, 0));
    if (handler == MAP_FAILED) {
      return MakeErrorCode(platform::com::ComErrc::kIllegalUseOfAllocate, "");
    }
    return {};
  }

  platform::core::Result<shm_type_t*> GetNewSamplesPointer() {
    if (shm_des <= 0) {
      return MakeErrorCode(platform::com::ComErrc::kSetHandlerNotSet, "");
    }
    if (handler == nullptr) {
      return MakeErrorCode(platform::com::ComErrc::kFieldValueIsNotValid, "");
    }
    return &this->handler->data;
  }
  platform::core::Result<shm_type_t> GetNewSamples() {
    if (shm_des <= 0) {
      return MakeErrorCode(platform::com::ComErrc::kSetHandlerNotSet, "");
    }
    if (handler == nullptr) {
      return MakeErrorCode(platform::com::ComErrc::kFieldValueIsNotValid, "");
    }

    pthread_mutex_lock(&handler->mutex_);
    const auto temp_value = handler->data;
    pthread_mutex_unlock(&handler->mutex_);

    return temp_value;
  }
  ~ShmProxy() { shm_unlink(instance_specifier_.ToString().c_str()); }
};
}  // namespace shm
}  // namespace com
}  // namespace bindings
}  // namespace srp
#endif  // BINDINGS_COMMON_SHM_SHM_PROXY_H_
