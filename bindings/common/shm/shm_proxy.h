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

#include "ara/com/com_error_domain.h"
#include "ara/core/instance_specifier.h"
#include "ara/core/result.h"

namespace srp {
namespace bindings {
namespace com {
namespace shm {
template <typename shm_type_t>
class ShmProxy final {
 private:
  const ara::core::InstanceSpecifier instance_specifier_;
  int shm_des{0};
  const int mode = S_IRWXU | S_IRWXG;
  struct shm_handler_t {
    pthread_mutex_t mutex_;
    shm_type_t data;
  };

  shm_handler_t* handler = nullptr;

 public:
  explicit ShmProxy(const ara::core::InstanceSpecifier& instance_specifier)
      : instance_specifier_{instance_specifier} {}

  ShmProxy(ShmProxy&) = delete;
  ShmProxy(ShmProxy&&) = delete;
  ShmProxy& operator=(ShmProxy&) = delete;
  ShmProxy& operator=(ShmProxy&&) = delete;

  ara::core::Result<void> FindService() noexcept {
    const int fd = shm_open(instance_specifier_.ToString().c_str(), O_RDWR, mode);
    if (fd <= 0) {
      return MakeErrorCode(ara::com::ComErrc::kNetworkBindingFailure, "");
    }
    if (ftruncate(fd, sizeof(shm_handler_t)) == -1) {
      close(fd);
      return MakeErrorCode(ara::com::ComErrc::kFieldValueIsNotValid, "");
    }
    void* const mapped = mmap(NULL, sizeof(shm_handler_t), PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
      close(fd);
      return MakeErrorCode(ara::com::ComErrc::kIllegalUseOfAllocate, "");
    }
    shm_des = fd;
    handler = reinterpret_cast<shm_handler_t*>(mapped);
    return {};
  }

  bool Attached() const noexcept { return handler != nullptr && shm_des > 0; }

  ara::core::Result<shm_type_t*> GetNewSamplesPointer() {
    if (shm_des <= 0) {
      return MakeErrorCode(ara::com::ComErrc::kSetHandlerNotSet, "");
    }
    if (handler == nullptr) {
      return MakeErrorCode(ara::com::ComErrc::kFieldValueIsNotValid, "");
    }
    return &this->handler->data;
  }
  ara::core::Result<shm_type_t> GetNewSamples() {
    if (shm_des <= 0) {
      return MakeErrorCode(ara::com::ComErrc::kSetHandlerNotSet, "");
    }
    if (handler == nullptr) {
      return MakeErrorCode(ara::com::ComErrc::kFieldValueIsNotValid, "");
    }

    pthread_mutex_lock(&handler->mutex_);
    const auto temp_value = handler->data;
    pthread_mutex_unlock(&handler->mutex_);

    return temp_value;
  }
  /// True when the skeleton was restarted: its constructor unlinks the name and creates a NEW segment, so
  /// the name now refers to another segment than the mapped one (this proxy would never see its data).
  bool Stale() const noexcept {
    if (handler == nullptr || shm_des <= 0) {
      return false;
    }
    const int fd = shm_open(instance_specifier_.ToString().c_str(), O_RDWR, mode);
    if (fd < 0) {
      return false;  // not offered yet: nothing to attach to
    }
    struct stat now {};
    struct stat mapped {};
    const bool stale = fstat(fd, &now) == 0 && fstat(shm_des, &mapped) == 0 &&
                       (now.st_ino != mapped.st_ino || now.st_dev != mapped.st_dev);
    close(fd);
    return stale;
  }

  /// Maps the current segment of the name instead of the stale one.
  /// On failure the proxy is left detached (Attached() == false, GetNewSamplesPointer() is an error).
  ara::core::Result<void> Reattach() noexcept {
    Detach();
    return FindService();
  }

  /// Unmaps and closes the segment (idempotent).
  void Detach() noexcept {
    if (handler != nullptr) {
      munmap(handler, sizeof(shm_handler_t));
    }
    if (shm_des > 0) {
      close(shm_des);
    }
    handler = nullptr;
    shm_des = 0;
  }

  ~ShmProxy() { shm_unlink(instance_specifier_.ToString().c_str()); }
};
}  // namespace shm
}  // namespace com
}  // namespace bindings
}  // namespace srp
#endif  // BINDINGS_COMMON_SHM_SHM_PROXY_H_
