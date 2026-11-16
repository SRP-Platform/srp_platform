/**
 * @file condition.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-01
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef CORE_COMMON_CONDITION_H_
#define CORE_COMMON_CONDITION_H_
#include <chrono>      // NOLINT
#include <semaphore>   // NOLINT
#include <stop_token>  // NOLINT

namespace srp {
namespace core {
namespace condition {
/**
 * @brief Interruptible sleep: returns after dur or as soon as stop is requested.
 *
 * Uses std::binary_semaphore instead of std::condition_variable(_any): with
 * C libraries lacking pthread_cond_clockwait (e.g. musl) libstdc++ measures
 * condition_variable timeouts with the wall clock, so a backward time step
 * extends the wait by the size of the step. The semaphore timeout is measured
 * with the steady clock on every C library.
 */
template <typename _Rep, typename _Period>
void wait_for(const std::chrono::duration<_Rep, _Period>& dur,
              const std::stop_token& stoken) {
  std::binary_semaphore stop_signal{0};
  const std::stop_callback stop_wait{stoken,
                                     [&stop_signal]() { stop_signal.release(); }};
  static_cast<void>(stop_signal.try_acquire_for(dur));
}

void wait(const std::stop_token& stoken);

}  // namespace condition
}  // namespace core
}  // namespace srp

#endif  // CORE_COMMON_CONDITION_H_
