/**
 * @file fifo_skeleton.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-15
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef BINDINGS_COMMON_FIFO_FIFO_SKELETON_H_
#define BINDINGS_COMMON_FIFO_FIFO_SKELETON_H_

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "platform/core/instance_specifier.h"
#include "platform/core/result.h"
#include "core/data/type_converter.h"

namespace srp {
namespace bindings {
namespace com {
namespace fifo {
template <typename fifo_type>
class FifoSkeleton {
 private:
  const platform::core::InstanceSpecifier instance_;

 public:
  explicit FifoSkeleton(const platform::core::InstanceSpecifier& instance)
      : instance_{instance} {}
  platform::core::Result<void> OfferService() {
    mkfifo(("/tmp/" + instance_.ToString()).c_str(), 0666);
    return {};
  }
  platform::core::Result<void> StopOfferService() {
    unlink(("/tmp/" + instance_.ToString()).c_str());
    return {};
  }
  platform::core::Result<void> Send(const fifo_type& value) {
    int fd = open(("/tmp/" + instance_.ToString()).c_str(), O_WRONLY);
    const auto data = srp::data::Convert2Vector<fifo_type>::Conv(value);
    write(fd, data.data(), data.size() + 1);
    close(fd);
    return {};
  }
};
}  // namespace fifo
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COMMON_FIFO_FIFO_SKELETON_H_
