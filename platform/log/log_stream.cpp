/**
 * @file log_stream.cpp
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-01
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "platform/log/log_stream.h"

#include <iomanip>
#include <sstream>
#include <string>

namespace platform {
namespace log {
void LogStream::concat(std::string &&log) { mLogs += log; }

LogStream &LogStream::operator<<(const LogStream &value) {
  mLogs += value.mLogs;

  return *this;
}

LogStream &LogStream::operator<<(bool value) {
  const std::string cTrueString = "true";
  const std::string cFalseString = "false";

  std::string _valueString;
  if (value) {
    _valueString = cTrueString;
  } else {
    _valueString = cFalseString;
  }
  concat(std::move(_valueString));

  return *this;
}

LogStream &LogStream::operator<<(const std::string &value) {
  mLogs += value;

  return *this;
}

LogStream &LogStream::operator<<(const char *value) {
  std::string _valueString{value};
  concat(std::move(_valueString));

  return *this;
}

LogStream &LogStream::operator<<(LogLevel value) {
  switch (value) {
    case LogLevel::kOff:
      mLogs += "Off";
      break;
    case LogLevel::kFatal:
      mLogs += "Fatal";
      break;
    case LogLevel::kError:
      mLogs += "Error";
      break;
    case LogLevel::kWarn:
      mLogs += "Warning";
      break;
    case LogLevel::kInfo:
      mLogs += "Info";
      break;
    case LogLevel::kDebug:
      mLogs += "Debug";
      break;
    case LogLevel::kVerbose:
      mLogs += "Verbose";
      break;
  }

  return *this;
}

LogStream &LogStream::operator<<(const platform::core::ErrorCode &value) {
  std::string _valueString = value.Message();
  _valueString+=": "+value.SupportData();
  concat(std::move(_valueString));

  return *this;
}

LogStream &LogStream::operator<<(
    const platform::core::InstanceSpecifier &value) noexcept {
  std::string _valueString = value.ToString();
  concat(std::move(_valueString));

  return *this;
}

LogStream &LogStream::operator<<(std::vector<std::uint8_t> value) {
  std::stringstream _stringstream;
  // Store bytes in the stream in hexadecimal format and fill with '0'.
  _stringstream << std::hex << std::setfill('0');

  for (uint8_t _byte : value) {
    uint32_t _byteInteger = static_cast<uint32_t>(_byte);
    // Set width to two chplatformcters (0x00).
    _stringstream << std::setw(2) << _byteInteger;
  }

  std::string _valueString = _stringstream.str();
  concat(std::move(_valueString));

  return *this;
}

LogStream &LogStream::WithLocation(std::string file, int line) {
  const std::string cSepplatformtor = ":";
  std::string _lineString = std::to_string(line);
  std::string _locationString = file + cSepplatformtor + _lineString;
  concat(std::move(_locationString));

  return *this;
}

std::string LogStream::ToString() const noexcept { return mLogs; }
}  // namespace log
}  // namespace platform
