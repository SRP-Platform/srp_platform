/**
 * @file i_monitor.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-08-11
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef PLATFORM_DIAG_I_MONITOR_H_
#define PLATFORM_DIAG_I_MONITOR_H_

#include "platform/core/instance_specifier.h"
#include "platform/core/result.h"
#include "platform/diag/monitor_types.h"

namespace platform {
namespace diag {
class IMonitor {
 public:
  virtual void ReportMonitorAction(MonitorAction action) = 0;
  virtual platform::core::Result<void> Offer() = 0;
};
}  // namespace diag
}  // namespace platform
#endif  // PLATFORM_DIAG_I_MONITOR_H_
