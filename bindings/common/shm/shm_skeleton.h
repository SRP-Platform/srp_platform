/**
 * @file shm_skeleton.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-01
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef BINDINGS_COMMON_SHM_SHM_SKELETON_H_
#define BINDINGS_COMMON_SHM_SHM_SKELETON_H_

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <bit>

#include "platform/com/com_error_domain.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/result.h"

namespace srp {
namespace bindings {
namespace com {
namespace shm {
template <typename shm_type_t>
class ShmSkeleton final {
 private:
  const platform::core::InstanceSpecifier instance_specifier_;
  int shm_des{0};
  const int mode = S_IRWXU | S_IRWXG;
  struct shm_handler_t {
    pthread_mutex_t mutex_;
    shm_type_t data;
  };
  shm_handler_t* handler = nullptr;
  pthread_mutexattr_t attrmutex;

 public:
  explicit ShmSkeleton(const platform::core::InstanceSpecifier& instance_specifier)
      : instance_specifier_{instance_specifier} {
    shm_unlink(instance_specifier_.ToString().c_str());
  }

  ShmSkeleton(ShmSkeleton&) = delete;
  ShmSkeleton(ShmSkeleton&&) = delete;
  ShmSkeleton& operator=(ShmSkeleton&) = delete;
  ShmSkeleton& operator=(ShmSkeleton&&) = delete;

  platform::core::Result<void> OfferService() noexcept {
    shm_des = shm_open(instance_specifier_.ToString().c_str(),
                       O_CREAT | O_RDWR | O_TRUNC, mode);
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
    pthread_mutexattr_init(&attrmutex);
    pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&handler->mutex_, &attrmutex);
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

  platform::core::Result<void> Send(const shm_type_t& data) {
    pthread_mutex_lock(&handler->mutex_);
    handler->data = data;
    pthread_mutex_unlock(&handler->mutex_);
    return {};
  }

  ~ShmSkeleton() {
    if (handler != nullptr) {
      pthread_mutex_destroy(&handler->mutex_);
      pthread_mutexattr_destroy(&attrmutex);
    }
    shmctl(shm_des, IPC_RMID, nullptr);
    shm_unlink(instance_specifier_.ToString().c_str());
  }
};
}  // namespace shm
}  // namespace com
}  // namespace bindings
}  // namespace srp
#endif  // BINDINGS_COMMON_SHM_SHM_SKELETON_H_
