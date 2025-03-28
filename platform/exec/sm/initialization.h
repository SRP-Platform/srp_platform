/**
 * @file initialization.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-01-11
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef PLATFORM_EXEC_SM_INITIALIZATION_H_
#define PLATFORM_EXEC_SM_INITIALIZATION_H_
#include <memory>

#include "platform/exec/sm/i_sm_com_wrapper.h"
#include "platform/exec/sm/i_state_controller.h"
namespace platform {
namespace exec {
namespace sm {
std::shared_ptr<IStateController> Initialize(
    std::unique_ptr<ISmComWrapper> com_wrapper) noexcept;
}  // namespace sm
}  // namespace exec
}  // namespace platform
#endif  // PLATFORM_EXEC_SM_INITIALIZATION_H_
