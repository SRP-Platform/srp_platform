/**
 * @file condition_variable.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-11
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_CORE_CONDITION_VARIABLE_H_
#define PLATFORM_CORE_CONDITION_VARIABLE_H_

#include <pthread.h>

#include <chrono>  // NOLINT
#include <functional>

#include "bindings/common/shm/shm_proxy.h"
#include "bindings/common/shm/shm_skeleton.h"
#include "platform/core/condition_variable_type.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/result.h"

namespace platform {
namespace core {

class ConditionVariableProxy final {
 public:
  using CheckCallback = std::function<bool()>;
  template <class Rep, class Period>
  using TimeDuration = std::chrono::duration<Rep, Period>;

 private:
  struct shm_object_t {
    pthread_cond_t cv_;
    pthread_mutex_t mutex_;
    uint8_t status_;
  };
  const InstanceSpecifier instance_;
  ConditionVariableProxy::shm_object_t* shm_object_;
  srp::bindings::com::shm::ShmProxy<ConditionVariableProxy::shm_object_t> shm_;

 public:
  ConditionVariableProxy(const InstanceSpecifier& instance);

  ConditionVariableProxy(ConditionVariableProxy&) = delete;
  ConditionVariableProxy(ConditionVariableProxy&&) = delete;
  ConditionVariableProxy& operator=(ConditionVariableProxy&) = delete;
  ConditionVariableProxy& operator=(ConditionVariableProxy&&) = delete;

  ~ConditionVariableProxy() = default;

  // platform::core::Result<void> Offer();
  platform::core::Result<void> Find();

  platform::core::Result<void> NotifyOne();
  platform::core::Result<void> NotifyAll();

  bool Wait(CheckCallback callback);
  bool Wait();
  template <class Rep, class Period>
  bool WaitFor(CheckCallback callback,
               TimeDuration<Rep, Period> time_duration) {
    return false;
  }
};

class ConditionVariableSkeleton final {
 public:
  using CheckCallback = std::function<bool()>;
  template <class Rep, class Period>
  using TimeDuration = std::chrono::duration<Rep, Period>;

 private:
  struct shm_object_t {
    pthread_cond_t cv_;
    pthread_mutex_t mutex_;
    uint8_t status_;
  };
  const InstanceSpecifier instance_;

  ConditionVariableSkeleton::shm_object_t* shm_object_;
  pthread_mutexattr_t attrmutex;
  pthread_condattr_t attrcond;
  srp::bindings::com::shm::ShmSkeleton<ConditionVariableSkeleton::shm_object_t> shm_;

 public:
  ConditionVariableSkeleton(const InstanceSpecifier& instance);

  ConditionVariableSkeleton(ConditionVariableSkeleton&) = delete;
  ConditionVariableSkeleton(ConditionVariableSkeleton&&) = delete;
  ConditionVariableSkeleton& operator=(ConditionVariableSkeleton&) = delete;
  ConditionVariableSkeleton& operator=(ConditionVariableSkeleton&&) = delete;

  ~ConditionVariableSkeleton();

  platform::core::Result<void> Offer();

  platform::core::Result<void> NotifyOne();
  platform::core::Result<void> NotifyAll();

  bool Wait(CheckCallback callback);
  bool Wait();
  template <class Rep, class Period>
  bool WaitFor(CheckCallback callback,
               TimeDuration<Rep, Period> time_duration) {
    return false;
  }
};

}  // namespace core
}  // namespace platform

#endif  // PLATFORM_CORE_CONDITION_VARIABLE_H_
