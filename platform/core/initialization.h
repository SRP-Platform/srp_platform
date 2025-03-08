/**
 * @file initialization.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-09-01
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef PLATFORM_CORE_INITIALIZATION_H_
#define PLATFORM_CORE_INITIALIZATION_H_
#include "platform/core/result.h"

namespace platform {
namespace core {
Result<void> Initialize() noexcept;

Result<void> Deinitialize() noexcept;
}  // namespace core
}  // namespace platform
#endif  // PLATFORM_CORE_INITIALIZATION_H_
