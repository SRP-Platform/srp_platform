/**
 * @file initialization.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-11-26
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef PLATFORM_COM_INITIALIZATION_H_
#define PLATFORM_COM_INITIALIZATION_H_
#include "platform/core/result.h"

namespace platform {
namespace com {
platform::core::Result<void> Initialize(const uint32_t& app_id) noexcept;

platform::core::Result<void> Deinitialize() noexcept;
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_INITIALIZATION_H_
