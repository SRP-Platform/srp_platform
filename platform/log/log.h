/**
 * @file log.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-09-01
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef PLATFORM_LOG_LOG_H_
#define PLATFORM_LOG_LOG_H_
#include "platform/log/logging_menager.h"
namespace platform {
namespace log {
/// @brief Create a stream for fatal logs
/// @returns Fatal log stream in the current context
/// @see WithLevel
static LogStream __attribute__((unused)) LogFatal() {
  return LoggingMenager::GetInstance()->CreateLogger("APP-").LogFatal();
}

/// @brief Create a stream for error logs
/// @returns Error log stream in the current context
/// @see WithLevel
static LogStream __attribute__((unused)) LogError() {
  return LoggingMenager::GetInstance()->CreateLogger("APP-").LogError();
}

/// @brief Create a stream for warning logs
/// @returns Warning log stream in the current context
/// @see WithLevel
static LogStream __attribute__((unused)) LogWarn() {
  return LoggingMenager::GetInstance()->CreateLogger("APP-").LogWarn();
}

/// @brief Create a stream for information logs
/// @returns Information log stream in the current context
/// @see WithLevel
static LogStream __attribute__((unused)) LogInfo() {
  return LoggingMenager::GetInstance()->CreateLogger("APP-").LogInfo();
}

/// @brief Create a stream for debug logs
/// @returns Debug log stream in the current context
/// @see WithLevel
static LogStream __attribute__((unused)) LogDebug() {
  return LoggingMenager::GetInstance()->CreateLogger("APP-").LogDebug();
}

/// @brief Create a stream for verbose logs
/// @returns Verbose log stream in the current context
/// @see WithLevel
static LogStream __attribute__((unused)) LogVerbose() {
  return LoggingMenager::GetInstance()->CreateLogger("APP-").LogVerbose();
}
}  // namespace log
}  // namespace platform

#endif  // PLATFORM_LOG_LOG_H_
