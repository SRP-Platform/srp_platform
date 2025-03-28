/**
 * @file example_skeleton.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef PLATFORM_COM_SKELETON_TESTS_EXAMPLE_EXAMPLE_SKELETON_H_
#define PLATFORM_COM_SKELETON_TESTS_EXAMPLE_EXAMPLE_SKELETON_H_

#include <memory>

#include "platform/com/skeleton/event.h"
#include "platform/com/skeleton/impl/method.h"
#include "platform/com/skeleton/skeleton.h"
#include "platform/com/skeleton/skeleton_packet_interpreter.h"

namespace platform {
namespace com {
namespace example {

class ExampleSkeletonInterface
    : public platform::com::skeleton::interpreter::SkeletonPacketInterpreter {
 private:
  /* data */
 public:
  ExampleSkeletonInterface(
      const platform::com::InstanceIdentifierContainer container,
      std::shared_ptr<platform::com::skeleton::wrapper::ISkeletonComWrapper>
          com_wrapper)
      : platform::com::skeleton::interpreter::
            SkeletonPacketInterpreter{container, com_wrapper},
        example_event{"example_event", *this} {}
  ~ExampleSkeletonInterface() = default;

  com::skeleton::Event<uint8_t> example_event;
};
using ExampleSkeleton =
    platform::com::skeleton::Skeleton<ExampleSkeletonInterface>;
}  // namespace example
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_SKELETON_TESTS_EXAMPLE_EXAMPLE_SKELETON_H_
