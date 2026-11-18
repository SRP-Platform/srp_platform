/**
 * Copyright (c) 2024
 *
 * @file mavlink_log.h
 * @brief Log levels and the log function shared by the MAVLink runtime (no ara::log dependency).
 */
#ifndef ARA_COM_MAVLINK_MAVLINK_LOG_H_
#define ARA_COM_MAVLINK_MAVLINK_LOG_H_

#include <functional>
#include <string>

namespace ara {
namespace com {
namespace mavlink {

enum class LogLevel { kDebug, kInfo, kWarn, kError };
using LogFn = std::function<void(LogLevel, const std::string&)>;

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_MAVLINK_LOG_H_
