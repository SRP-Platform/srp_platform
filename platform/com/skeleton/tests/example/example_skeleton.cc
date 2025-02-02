/**
 * @file example_skeleton.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "platform/com/skeleton/tests/example/example_skeleton.h"

void test() {
  auto test = platform::com::example::ExampleSkeleton::Create(
      platform::core::InstanceSpecifier{"test"});
  test.Value()->OfferService();
  test.Value()->StopOfferService();
}
