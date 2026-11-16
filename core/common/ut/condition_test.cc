/**
 * @file condition_test.cc
 * @author Mateusz Krajewski (matikrajek42@gmail.com)
 * @brief Unit tests for interruptible waits (core::condition)
 * @version 0.1
 * @date 2026-11-16
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <gtest/gtest.h>

#include <chrono>      // NOLINT
#include <stop_token>  // NOLINT
#include <thread>      // NOLINT

#include "core/common/condition.h"

namespace {
using Clock = std::chrono::steady_clock;

int64_t ElapsedMs(const Clock::time_point& start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                               start)
      .count();
}
}  // namespace

TEST(ConditionWaitFor, ReturnsAfterTimeout) {
  std::stop_source source;
  const auto start = Clock::now();
  srp::core::condition::wait_for(std::chrono::milliseconds(200),
                                 source.get_token());
  const auto elapsed = ElapsedMs(start);
  EXPECT_GE(elapsed, 200);
  EXPECT_LT(elapsed, 1000);
}

TEST(ConditionWaitFor, ReturnsImmediatelyWhenAlreadyStopped) {
  std::stop_source source;
  source.request_stop();
  const auto start = Clock::now();
  srp::core::condition::wait_for(std::chrono::seconds(5), source.get_token());
  EXPECT_LT(ElapsedMs(start), 500);
}

TEST(ConditionWaitFor, ReturnsImmediatelyForZeroAndNegativeDuration) {
  std::stop_source source;
  const auto start = Clock::now();
  srp::core::condition::wait_for(std::chrono::milliseconds(0),
                                 source.get_token());
  srp::core::condition::wait_for(std::chrono::milliseconds(-100),
                                 source.get_token());
  EXPECT_LT(ElapsedMs(start), 500);
}

TEST(ConditionWaitFor, StopRequestInterruptsWait) {
  std::stop_source source;
  std::thread stopper([&source]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    source.request_stop();
  });
  const auto start = Clock::now();
  srp::core::condition::wait_for(std::chrono::seconds(10), source.get_token());
  const auto elapsed = ElapsedMs(start);
  stopper.join();
  EXPECT_GE(elapsed, 100);
  EXPECT_LT(elapsed, 2000);
}

TEST(ConditionWaitFor, VeryLongDurationIsInterruptible) {
  std::stop_source source;
  std::thread stopper([&source]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    source.request_stop();
  });
  const auto start = Clock::now();
  srp::core::condition::wait_for(std::chrono::hours(24 * 365 * 100),
                                 source.get_token());
  stopper.join();
  EXPECT_LT(ElapsedMs(start), 2000);
}

TEST(ConditionWaitFor, WorksWithJthreadStopToken) {
  const auto start = Clock::now();
  {
    std::jthread worker([](const std::stop_token& token) {
      while (!token.stop_requested()) {
        srp::core::condition::wait_for(std::chrono::seconds(10), token);
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }  // jthread destructor requests stop and joins
  EXPECT_LT(ElapsedMs(start), 2000);
}

TEST(ConditionWait, ReturnsWhenStopRequested) {
  std::stop_source source;
  std::thread stopper([&source]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    source.request_stop();
  });
  const auto start = Clock::now();
  srp::core::condition::wait(source.get_token());
  const auto elapsed = ElapsedMs(start);
  stopper.join();
  EXPECT_GE(elapsed, 100);
  EXPECT_LT(elapsed, 2000);
}

TEST(ConditionWait, ReturnsImmediatelyWhenAlreadyStopped) {
  std::stop_source source;
  source.request_stop();
  const auto start = Clock::now();
  srp::core::condition::wait(source.get_token());
  EXPECT_LT(ElapsedMs(start), 500);
}
