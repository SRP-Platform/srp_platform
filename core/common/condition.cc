/**
 * @file condition.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-15
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "core/common/condition.h"

namespace srp {
namespace core {
namespace condition {

void wait(const std::stop_token& stoken) {
  std::binary_semaphore stop_signal{0};
  const std::stop_callback stop_wait{stoken,
                                     [&stop_signal]() { stop_signal.release(); }};
  stop_signal.acquire();
}

}  // namespace condition
}  // namespace core
}  // namespace srp
