/**
 * @file test_condition_variable.cc
 * @author Mateusz Krajewski (matikrajek42@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-12-01
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#include <gtest/gtest.h>
#include <thread>  // NOLINT
#include "platform/core/condition_variable.h"
#include "platform/core/instance_specifier.h"
#include "platform/core/core_error_domain.h"

using namespace platform::core;  // NOLINT


TEST(ConditionVariableProxyTest, FindServiceSuccess) {
  ConditionVariableProxy proxy(InstanceSpecifier("mock"));
  EXPECT_FALSE(proxy.Find().HasValue());
}


TEST(ConditionVariableProxyTest, WaitFail) {
  ConditionVariableProxy proxy(InstanceSpecifier{"mock"});

  proxy.Find();

  std::thread notifier([&proxy]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    proxy.NotifyOne();
  });

  bool wait_result = proxy.Wait();
  EXPECT_FALSE(wait_result);

  notifier.join();
}

TEST(ConditionVariableSkeletonTest, OfferSuccess) {
  ConditionVariableSkeleton skeleton(InstanceSpecifier{"mock"});
  auto result = skeleton.Offer();
  EXPECT_TRUE(result.HasValue());
}

TEST(ConditionVariableSkeletonTest, NotifyAllSuccess) {
  ConditionVariableSkeleton skeleton(InstanceSpecifier{"mock"});

  skeleton.Offer();

  auto result = skeleton.NotifyAll();
  EXPECT_TRUE(result.HasValue());
}

TEST(ConditionVariableSkeletonTest, WaitSuccess) {
  ConditionVariableSkeleton skeleton(InstanceSpecifier{"mock"});

  skeleton.Offer();

  std::thread notifier([&skeleton]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    skeleton.NotifyOne();
  });

  bool wait_result = skeleton.Wait();
  EXPECT_TRUE(wait_result);

  notifier.join();
}
