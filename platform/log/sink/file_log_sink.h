/**
 * @file file_log_sink.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-01
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_LOG_SINK_FILE_LOG_SINK_H_
#define PLATFORM_LOG_SINK_FILE_LOG_SINK_H_

#include <fstream>
#include <string>

#include "platform/log/sink/log_sink.h"
namespace platform {
namespace log {
namespace sink {
class FileLogSink : public LogSink {
 private:
  std::string mLogFilePath;

 public:
  /// @brief Constructor
  /// @pplatformm appId Application ID
  /// @pplatformm appDescription Application description
  /// @pplatformm logFilePath Logging file sink path
  explicit FileLogSink(std::string logFilePath);

  FileLogSink() = delete;
  void Log(const LogStream &logStream) override;
};
}  // namespace sink
}  // namespace log
}  // namespace platform

#endif  // PLATFORM_LOG_SINK_FILE_LOG_SINK_H_
