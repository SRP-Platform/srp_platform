/**
 * @file fifo_proxy.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-15
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef BINDINGS_COMMON_FIFO_FIFO_PROXY_H_
#define BINDINGS_COMMON_FIFO_FIFO_PROXY_H_

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <vector>

#include "platform/com/com_error_domain.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/result.h"
#include "core/data/type_converter.h"

namespace srp {
namespace bindings {
namespace com {
namespace fifo {
template <typename fifo_type>
class FifoProxy {
 private:
  const platform::core::InstanceSpecifier instance_;

 public:
  explicit FifoProxy(const platform::core::InstanceSpecifier& instance)
      : instance_{instance} {}
  platform::core::Result<void> FindService() {
    // mkfifo(("/tmp/" + instance_.ToString()).c_str(), 0666);
    return {};
  }
  platform::core::Result<void> StopOfferService() { return {}; }
  platform::core::Result<fifo_type> GetNewSamples() {
    int fd = open(("/tmp/" + instance_.ToString()).c_str(), O_RDONLY);
    if (fd < 0) {
      return platform::com::MakeErrorCode(platform::com::ComErrc::kIllegalUseOfAllocate,
                                     "fifo not exist!");
    }
    std::array<uint8_t, sizeof(fifo_type)> buffor{};
    // const auto data = srp::data::Convert2Vector<fifo_type>::Conv(value);
    const auto res = read(fd, buffor.data(), sizeof(fifo_type));
    close(fd);
    if (res > 0) {
      const auto data = srp::data::Convert<fifo_type>::Conv(
          std::vector<uint8_t>{buffor.begin(), buffor.begin() + res});
      if (data.has_value()) {
        return data.value();
      }
      return platform::com::MakeErrorCode(platform::com::ComErrc::kPeerIsUnreachable,
                                     " Error 1");
    }
    return platform::com::MakeErrorCode(platform::com::ComErrc::kCommunicationStackError,
                                   " Error 2");
  }
};
}  // namespace fifo
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COMMON_FIFO_FIFO_PROXY_H_
