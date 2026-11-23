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
#include <unistd.h>
#include <atomic>
#include <string>
#include <chrono>  // NOLINT
#include <memory>
#include <thread>  // NOLINT
#include "ara/core/condition_variable.h"
#include "ara/core/instance_specifier.h"
#include "ara/core/core_error_domain.h"

using namespace ara::core;  // NOLINT


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

// A respawn of the offering application: the new skeleton creates a NEW shared segment, so a proxy attached
// to the old one never sees its notifications. The proxy must notice it (Stale) and attach to the new one.
namespace {
// The shared-memory names are global: unique per process, so parallel runs of the test do not collide.
const std::string& UniqueSuffix() {
  static const std::string suffix = std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000);
  return suffix;
}
}  // namespace

TEST(ConditionVariableRestartTest, ProxyReattachesToARestartedSkeleton) {
  auto old_skeleton = std::make_unique<ConditionVariableSkeleton>(InstanceSpecifier{"restart_cv_" + UniqueSuffix()});
  ASSERT_TRUE(old_skeleton->Offer().HasValue());
  ConditionVariableProxy proxy(InstanceSpecifier{"restart_cv_" + UniqueSuffix()});
  ASSERT_TRUE(proxy.Find().HasValue());
  EXPECT_FALSE(proxy.Stale());
  // "kill -9" of the old application: its destructor never runs (the object is leaked below)
  ConditionVariableSkeleton restarted(InstanceSpecifier{"restart_cv_" + UniqueSuffix()});
  ASSERT_TRUE(restarted.Offer().HasValue());
  EXPECT_TRUE(proxy.Stale());
  ASSERT_TRUE(proxy.Reattach().HasValue());
  EXPECT_FALSE(proxy.Stale());
  std::atomic<bool> woken{false};
  std::thread waiter([&] { woken = proxy.WaitFor(std::chrono::milliseconds{3000}); });
  for (int i = 0; i < 20 && !woken.load(); ++i) {
    restarted.NotifyAll();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  waiter.join();
  EXPECT_TRUE(woken.load()) << "the re-attached proxy was not woken by the restarted skeleton";
  old_skeleton.release();  // leaked on purpose (its destructor would tear down the shared state)
}

TEST(ConditionVariableProxyTest, WaitForTimesOutWithoutANotification) {
  ConditionVariableSkeleton skeleton(InstanceSpecifier{"waitfor_cv_" + UniqueSuffix()});
  ASSERT_TRUE(skeleton.Offer().HasValue());
  ConditionVariableProxy proxy(InstanceSpecifier{"waitfor_cv_" + UniqueSuffix()});
  ASSERT_TRUE(proxy.Find().HasValue());
  const auto t0 = std::chrono::steady_clock::now();
  EXPECT_FALSE(proxy.WaitFor(std::chrono::milliseconds{150}));
  EXPECT_GE(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds{140});
}

// The segment vanished between Stale() and Reattach() (e.g. a short-lived daemon): Reattach fails and leaves the
// proxy detached, which stays harmless (no dereference of a null pointer, no busy loop in WaitFor).
TEST(ConditionVariableRestartTest, FailedReattachLeavesTheProxyDetached) {
  auto skeleton = std::make_unique<ConditionVariableSkeleton>(InstanceSpecifier{"vanish_cv_" + UniqueSuffix()});
  ASSERT_TRUE(skeleton->Offer().HasValue());
  ConditionVariableProxy proxy(InstanceSpecifier{"vanish_cv_" + UniqueSuffix()});
  ASSERT_TRUE(proxy.Find().HasValue());
  EXPECT_TRUE(proxy.Attached());
  skeleton.reset();  // unlinks the segment
  EXPECT_FALSE(proxy.Reattach().HasValue());
  EXPECT_FALSE(proxy.Attached());
  EXPECT_FALSE(proxy.Stale());
  EXPECT_FALSE(proxy.WaitFor(std::chrono::milliseconds{50}));
  EXPECT_FALSE(proxy.NotifyAll().HasValue());
  // a skeleton comes back: the same proxy attaches again
  ConditionVariableSkeleton back(InstanceSpecifier{"vanish_cv_" + UniqueSuffix()});
  ASSERT_TRUE(back.Offer().HasValue());
  ASSERT_TRUE(proxy.Reattach().HasValue());
  EXPECT_TRUE(proxy.Attached());
}
