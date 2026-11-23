/**
 * @file ipc_restart_test.cc
 * @copyright Copyright (c) 2024
 * @brief The `ipc` binding across a restart of the skeleton side (a respawned application): the events of the
 * new skeleton must reach the proxy that was started against the previous one; while the skeleton is away the
 * proxy must stay alive and idle (no crash, no busy loop), also when re-attaching fails.
 */
#include <gtest/gtest.h>
#include <sys/resource.h>
#include <unistd.h>

#include <atomic>
#include <chrono>  // NOLINT
#include <memory>
#include <stop_token>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/someip/bindings/ipc_bindings.h"
#include "ara/log/logging_menager.h"

namespace ara {
namespace com {
namespace someip {
namespace bindings {
namespace {
using std::chrono::milliseconds;

// The shared-memory names are global: unique per process, so parallel runs of the test do not collide.
std::string Name(const char* base) {
  static const std::string suffix =
      std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000);
  return std::string(base) + "_" + suffix;
}

// Process CPU time (user + system) in seconds.
double CpuSeconds() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
         static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
}

// Proxy whose first `fail_attach` re-attach attempts fail (an injected shm_open/mmap error).
class FlakyProxy : public IPCProxyBindings {
 public:
  FlakyProxy(const std::string& path, int fail_attach) : IPCProxyBindings{path}, fail_attach_{fail_attach} {}
  std::atomic<int> attempts{0};

 protected:
  bool TryAttach() override {
    ++attempts;
    if (fail_attach_ > 0) {
      --fail_attach_;
      return false;
    }
    return IPCProxyBindings::TryAttach();
  }

 private:
  std::atomic<int> fail_attach_;
};

// Publishes `HandleEvent` every 20 ms until `want` more events were received (or ~3 s).
bool EventsArrive(IPCSkeletonBindings* skeleton, const std::atomic<int>& received, int want) {
  const int start = received.load();
  for (int i = 0; i < 150 && received.load() < start + want; ++i) {
    skeleton->HandleEvent(7, std::vector<uint8_t>{1, 2, 3});
    std::this_thread::sleep_for(milliseconds(20));
  }
  return received.load() >= start + want;
}

class IpcRestart : public ::testing::Test {
 protected:
  void SetUp() override {
    ara::log::LoggingMenager::Create("IPC-", ara::log::LogMode::kConsole, ara::log::LogLevel::kWarn);
  }
  void StartProxy(IPCProxyBindings* proxy) {
    proxy->SetProxyCallback([this](uint16_t, std::vector<uint8_t>) { ++received_; }, [](uint16_t, auto) {});
    starter_ = std::jthread{[proxy](std::stop_token t) { proxy->Start(t); }};
  }
  void TearDown() override {
    stop_.request_stop();
    starter_.request_stop();
    if (starter_.joinable()) {
      starter_.join();
    }
  }
  std::stop_source stop_;
  std::atomic<int> received_{0};
  std::jthread starter_;
};

// (a) a respawn with the destructors run (normal exit: the segments are unlinked), a new skeleton later
TEST_F(IpcRestart, ProxyReceivesTheEventsAfterANormalRestart) {
  auto skeleton = std::make_unique<IPCSkeletonBindings>(Name("srp_ipc_restart_a"));
  skeleton->Start(stop_.get_token());
  IPCProxyBindings proxy{Name("srp_ipc_restart_a")};
  StartProxy(&proxy);
  ASSERT_TRUE(EventsArrive(skeleton.get(), received_, 3)) << "events of the first skeleton";

  skeleton.reset();  // destructors: shm_unlink of the segments
  std::this_thread::sleep_for(milliseconds(500));
  IPCSkeletonBindings restarted{Name("srp_ipc_restart_a")};
  restarted.Start(stop_.get_token());
  EXPECT_TRUE(EventsArrive(&restarted, received_, 3)) << "events of the restarted skeleton";
}

// (a2) the offering application is killed (its destructors never run) and respawned at once
TEST_F(IpcRestart, ProxyReceivesTheEventsAfterAKilledSkeleton) {
  auto old_skeleton = std::make_unique<IPCSkeletonBindings>(Name("srp_ipc_restart_a2"));
  old_skeleton->Start(stop_.get_token());
  IPCProxyBindings proxy{Name("srp_ipc_restart_a2")};
  StartProxy(&proxy);
  ASSERT_TRUE(EventsArrive(old_skeleton.get(), received_, 3)) << "events of the first skeleton";

  IPCSkeletonBindings restarted{Name("srp_ipc_restart_a2")};
  restarted.Start(stop_.get_token());
  EXPECT_TRUE(EventsArrive(&restarted, received_, 3)) << "events of the restarted skeleton";
  old_skeleton.release();  // leaked on purpose (a killed process runs no destructors)
}

// (b) the skeleton does not come back for 3 s: the client lives and does not burn the CPU; then it returns
TEST_F(IpcRestart, ProxyIdlesWhileTheSkeletonIsAwayAndRecovers) {
  auto skeleton = std::make_unique<IPCSkeletonBindings>(Name("srp_ipc_restart_b"));
  skeleton->Start(stop_.get_token());
  IPCProxyBindings proxy{Name("srp_ipc_restart_b")};
  StartProxy(&proxy);
  ASSERT_TRUE(EventsArrive(skeleton.get(), received_, 3));

  skeleton.reset();
  std::this_thread::sleep_for(milliseconds(300));  // let the proxy settle
  const double cpu0 = CpuSeconds();
  std::this_thread::sleep_for(std::chrono::seconds(3));
  const double load = (CpuSeconds() - cpu0) / 3.0;
  EXPECT_LT(load, 0.05) << "the proxy of an absent skeleton uses " << load * 100 << "% CPU";

  IPCSkeletonBindings back{Name("srp_ipc_restart_b")};
  back.Start(stop_.get_token());
  EXPECT_TRUE(EventsArrive(&back, received_, 3)) << "events after the skeleton came back";
}

// (c) re-attaching fails (injected): no crash (nullptr dereference), no busy loop, and it recovers
TEST_F(IpcRestart, FailedReattachNeitherCrashesNorSpins) {
  auto old_skeleton = std::make_unique<IPCSkeletonBindings>(Name("srp_ipc_restart_c"));
  old_skeleton->Start(stop_.get_token());
  FlakyProxy proxy{Name("srp_ipc_restart_c"), 1000};  // fails until the end of the test
  StartProxy(&proxy);
  ASSERT_TRUE(EventsArrive(old_skeleton.get(), received_, 3));

  IPCSkeletonBindings restarted{Name("srp_ipc_restart_c")};
  restarted.Start(stop_.get_token());
  old_skeleton.release();
  std::this_thread::sleep_for(milliseconds(500));  // the proxy notices the restart and fails to attach
  ASSERT_GE(proxy.attempts.load(), 1);
  const int attempts0 = proxy.attempts.load();
  const double cpu0 = CpuSeconds();
  std::this_thread::sleep_for(std::chrono::seconds(3));
  const double load = (CpuSeconds() - cpu0) / 3.0;
  EXPECT_LT(load, 0.05) << "a proxy that cannot re-attach uses " << load * 100 << "% CPU";
  // backoff 200 ms -> 1 s: a handful of attempts in 3 s, never a flood
  EXPECT_LE(proxy.attempts.load() - attempts0, 8);
  EXPECT_GE(proxy.attempts.load() - attempts0, 2);
}

TEST_F(IpcRestart, ReattachRecoversAfterTransientFailures) {
  auto old_skeleton = std::make_unique<IPCSkeletonBindings>(Name("srp_ipc_restart_c2"));
  old_skeleton->Start(stop_.get_token());
  FlakyProxy proxy{Name("srp_ipc_restart_c2"), 3};
  StartProxy(&proxy);
  ASSERT_TRUE(EventsArrive(old_skeleton.get(), received_, 3));

  IPCSkeletonBindings restarted{Name("srp_ipc_restart_c2")};
  restarted.Start(stop_.get_token());
  old_skeleton.release();
  EXPECT_TRUE(EventsArrive(&restarted, received_, 3)) << "events after 3 failed re-attach attempts";
  EXPECT_GE(proxy.attempts.load(), 4);
}
}  // namespace
}  // namespace bindings
}  // namespace someip
}  // namespace com
}  // namespace ara
