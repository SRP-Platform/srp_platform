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

#ifndef BINDINGS_COMMON_SHM_SHM_BUFFOR_PROXY_H_
#define BINDINGS_COMMON_SHM_SHM_BUFFOR_PROXY_H_

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "platform/com/com_error_domain.h"
#include "platform/core/condition_variable.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/result.h"

namespace srp {
namespace bindings {
namespace com {
namespace shm {
template <std::size_t buff_size>
class ShmBufforProxy final {
 private:
  const platform::core::InstanceSpecifier instance_specifier_;
  int shm_des{0};
  const int mode = S_IRWXU | S_IRWXG;
  std::uint8_t* handler = nullptr;

 public:
  explicit ShmBufforProxy(
      const platform::core::InstanceSpecifier& instance_specifier)
      : instance_specifier_{instance_specifier} {}

  ShmBufforProxy(ShmBufforProxy&) = delete;
  ShmBufforProxy(ShmBufforProxy&&) = delete;
  ShmBufforProxy& operator=(ShmBufforProxy&) = delete;
  ShmBufforProxy& operator=(ShmBufforProxy&&) = delete;

  platform::core::Result<void> FindService() noexcept {
    shm_des = shm_open(instance_specifier_.ToString().c_str(), O_RDWR, mode);
    if (shm_des <= 0) {
      return MakeErrorCode(platform::com::ComErrc::kNetworkBindingFailure, "");
    }
    if (ftruncate(shm_des, buff_size) == -1) {
      return MakeErrorCode(platform::com::ComErrc::kFieldValueIsNotValid, "");
    }
    handler = reinterpret_cast<std::uint8_t*>(
        mmap(NULL, buff_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_des, 0));
    return {};
  }

  uint8_t* GetNewSamplesPointer() { return handler; }
  ~ShmBufforProxy() { shm_unlink(instance_specifier_.ToString().c_str()); }
};
}  // namespace shm
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COMMON_SHM_SHM_BUFFOR_PROXY_H_
