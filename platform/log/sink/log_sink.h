/**
 * @file log_sink.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-09-01
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef PLATFORM_LOG_SINK_LOG_SINK_H_
#define PLATFORM_LOG_SINK_LOG_SINK_H_

#include <ctime>

#include "platform/log/log_stream.h"

namespace platform {
namespace log {
namespace sink {
/// @brief Application logging sink
class LogSink {
 protected:
  /// @brief Get the application stamp (application ID and description)
  /// @returns A log stream with the application stamp
  LogStream GetAppstamp() const;

  /// @brief Get the current timestamp at the function call
  /// @brief A log stream with the current long format date-time
  LogStream GetTimestamp() const;
  uint32_t GetTimeStamp() const noexcept;

 public:
  LogSink() = default;
  virtual ~LogSink() noexcept = default;

  /// @brief Log a stream corresponds to the current application
  /// @pplatformm logStream Input log stream
  virtual void Log(const LogStream &logStream) = 0;
};
}  // namespace sink
}  // namespace log
}  // namespace platform

#endif  // PLATFORM_LOG_SINK_LOG_SINK_H_
