/**
 * @file em_execution_test.cc
 * @author Mateusz Krajewski (matikrajek42@gmail.com)
 * @brief Integration tests for ExecutionClient <-> EM app-state handling.
 * @version 0.1
 * @date 2026-09-11
 *
 * @copyright Copyright (c) 2026
 */
#include <gtest/gtest.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>  // NOLINT
#include <map>
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/i_com_client.h"
#include "ara/exec/adaptive_application.h"
#include "ara/exec/adaptive_lifecycle_menager.h"
#include "ara/exec/em/execution_client.h"
#include "ara/log/logging_menager.h"
#include "platform/common/em/code/services/em/app_db.h"
#include "platform/common/em/code/services/em/em_service.h"
#include "srp/platform/em/ExecutionHeader.h"

namespace {

constexpr uint16_t kAppId = 0x1234;
constexpr uint16_t kFgStart = 1;
constexpr uint16_t kFgRun = 2;

void EnsureLogging() {
  if (ara::log::LoggingMenager::GetInstance() == nullptr) {
    ara::log::LoggingMenager::Create("TEST", ara::log::LogMode::kConsole,
                                     ara::log::LogLevel::kWarn);
  }
}

std::vector<uint8_t> MakeExecFrame(uint32_t app_id,
                                   ara::exec::ExecutionState state) {
  srp::platform::em::ExecutionHeader hdr{};
  hdr.app_id = app_id;
  hdr.execution_state = static_cast<uint8_t>(state);
  auto payload =
      srp::data::Convert2Vector<srp::platform::em::ExecutionHeader>::Conv(hdr);
  std::vector<uint8_t> frame{ara::com::IComClient::MsgType::kExec};
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

class DummyApp : public ara::exec::AdaptiveApplication {
 public:
  static int init_ret;
  static std::atomic<bool> ran;

  int Initialize(const std::map<ara::core::StringView, ara::core::StringView>
                     parms) override {
    (void)parms;
    return init_ret;
  }
  int Run(const std::stop_token& token) override {
    ran = true;
    while (!token.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
  }
};

int DummyApp::init_ret = 0;
std::atomic<bool> DummyApp::ran{false};

class EmExecutionTest : public ::testing::Test {
 protected:
  std::shared_ptr<srp::em::service::data::AppDb> db_;
  std::unique_ptr<srp::em::service::EmService> em_;
  uint16_t sm_confirmed_{0};

  void SetUp() override {
    EnsureLogging();
    db_ = std::make_shared<srp::em::service::data::AppDb>();
    ASSERT_EQ(0, db_->InsertNewFG(kFgStart, "Startup"));
    ASSERT_EQ(0, db_->InsertNewFG(kFgRun, "Running"));
    sm_confirmed_ = 0;
    em_ = std::make_unique<srp::em::service::EmService>(
        db_,
        [this](const uint16_t& id) { sm_confirmed_ = id; },
        /*start_timeout_ms=*/300, /*stop_timeout_ms=*/300);
  }

  void TearDown() override {
    auto client = ara::exec::ExecutionClient::GetInstance();
    client->SetSendCallback(
        ara::com::IComClient::SendCallback{},
        ara::com::IComClient::SendCallbackTo{});
    em_.reset();
  }

  void InsertApp(uint16_t app_id, const std::string& bin = "/no/such/em_bin") {
    srp::em::service::data::AppConfig cfg{bin, "", app_id, "test_app",
                                         {"Startup", "Running"}};
    ASSERT_EQ(0, db_->InsertNewApp(cfg));
  }
};

TEST_F(EmExecutionTest, ClientReportUpdatesEmDb) {
  InsertApp(kAppId);
  std::vector<ara::exec::ExecutionState> seen;
  auto client = ara::exec::ExecutionClient::GetInstance(kAppId);
  client->SetSendCallback(
      [](uint32_t, const std::vector<uint8_t>&, ara::com::IComClient::MsgType) {
        return true;
      },
      [this, &seen](const std::string& desc, const std::vector<uint8_t>& payload,
                    ara::com::IComClient::MsgType type) {
        EXPECT_EQ("ARA.EXEC", desc);
        std::vector<uint8_t> frame{static_cast<uint8_t>(type)};
        frame.insert(frame.end(), payload.begin(), payload.end());
        em_->HandleExecFrame(frame);
        auto hdr =
            srp::data::Convert<srp::platform::em::ExecutionHeader>::Conv(
                payload);
        if (hdr.has_value()) {
          seen.push_back(static_cast<ara::exec::ExecutionState>(
              hdr.value().execution_state));
        }
        return true;
      });

  ASSERT_TRUE(client->ReportExecutionState(ara::exec::ExecutionState::kIdle)
                  .HasValue());
  ASSERT_TRUE(client->ReportExecutionState(ara::exec::ExecutionState::kStarting)
                  .HasValue());
  ASSERT_TRUE(client->ReportExecutionState(ara::exec::ExecutionState::kRunning)
                  .HasValue());

  const auto state = db_->GetExecutionStateForApp(kAppId);
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(ara::exec::ExecutionState::kRunning, state.value());
  ASSERT_EQ(3U, seen.size());
  EXPECT_EQ(ara::exec::ExecutionState::kIdle, seen[0]);
  EXPECT_EQ(ara::exec::ExecutionState::kStarting, seen[1]);
  EXPECT_EQ(ara::exec::ExecutionState::kRunning, seen[2]);
}

TEST_F(EmExecutionTest, WaitForAppStatusUnblocksOnReport) {
  InsertApp(kAppId);
  std::atomic<bool> started{false};
  std::thread reporter([this, &started]() {
    while (!started) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    em_->HandleExecFrame(
        MakeExecFrame(kAppId, ara::exec::ExecutionState::kRunning));
  });
  started = true;
  EXPECT_TRUE(em_->WaitForAppStatus(kAppId, ara::exec::ExecutionState::kRunning,
                                    1000));
  reporter.join();
}

TEST_F(EmExecutionTest, SpawnFailureDoesNotConfirmSm) {
  InsertApp(kAppId, "/no/such/em_bin");
  em_->SetActiveState(kFgRun);
  EXPECT_EQ(0, sm_confirmed_);
  EXPECT_EQ(0, db_->GetPidForApp(kAppId));
  const auto state = db_->GetExecutionStateForApp(kAppId);
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(ara::exec::ExecutionState::kErrorShutdown, state.value());
}

TEST_F(EmExecutionTest, UnsupportedFgIsIgnored) {
  InsertApp(kAppId);
  em_->SetActiveState(999);
  EXPECT_EQ(0, sm_confirmed_);
  EXPECT_EQ(0, em_->GetActiveState());
}

TEST_F(EmExecutionTest, WatchdogDetectsDeadProcess) {
  InsertApp(kAppId);
  em_->SetActiveState(kFgRun);
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    pause();
    _exit(0);
  }
  db_->SetPidForApp(kAppId, static_cast<uint32_t>(child));
  ASSERT_EQ(0, kill(child, SIGKILL));
  int status = 0;
  ASSERT_EQ(child, waitpid(child, &status, 0));

  em_->CheckAliveApps();
  EXPECT_EQ(0, db_->GetPidForApp(kAppId));
  const auto state = db_->GetExecutionStateForApp(kAppId);
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(ara::exec::ExecutionState::kErrorShutdown, state.value());
}

TEST_F(EmExecutionTest, UnknownAppReportIsIgnored) {
  em_->HandleExecFrame(
      MakeExecFrame(0xBEEF, ara::exec::ExecutionState::kRunning));
  EXPECT_FALSE(db_->GetExecutionStateForApp(0xBEEF).has_value());
}

TEST(EmLifecycleTest, InitFailureReportsErrorAndSkipsRun) {
  EnsureLogging();
  DummyApp::init_ret = -7;
  DummyApp::ran = false;
  std::vector<ara::exec::ExecutionState> seen;
  auto client = ara::exec::ExecutionClient::GetInstance(1);
  client->SetSendCallback(
      [](uint32_t, const std::vector<uint8_t>&, ara::com::IComClient::MsgType) {
        return true;
      },
      [&seen](const std::string&, const std::vector<uint8_t>& payload,
              ara::com::IComClient::MsgType) {
        auto hdr =
            srp::data::Convert<srp::platform::em::ExecutionHeader>::Conv(
                payload);
        if (hdr.has_value()) {
          seen.push_back(static_cast<ara::exec::ExecutionState>(
              hdr.value().execution_state));
        }
        return true;
      });

  const char* argv[] = {"/tmp/em_test/bin/app"};
  ara::exec::AdaptiveLifecycleMenager::CreateAdaptiveLifecycleMenager<DummyApp>(
      1, argv, true);
  EXPECT_EQ(-7, ara::exec::AdaptiveLifecycleMenager::StartAdaptiveLifecycleMenager());
  EXPECT_FALSE(DummyApp::ran.load());
  ASSERT_GE(seen.size(), 3U);
  EXPECT_EQ(ara::exec::ExecutionState::kIdle, seen[0]);
  EXPECT_EQ(ara::exec::ExecutionState::kStarting, seen[1]);
  EXPECT_EQ(ara::exec::ExecutionState::kErrorShutdown, seen[2]);
}

TEST(EmLifecycleTest, SuccessfulInitReportsRunningThenTerminating) {
  EnsureLogging();
  DummyApp::init_ret = 0;
  DummyApp::ran = false;
  std::vector<ara::exec::ExecutionState> seen;
  auto client = ara::exec::ExecutionClient::GetInstance(1);
  client->SetSendCallback(
      [](uint32_t, const std::vector<uint8_t>&, ara::com::IComClient::MsgType) {
        return true;
      },
      [&seen](const std::string&, const std::vector<uint8_t>& payload,
              ara::com::IComClient::MsgType) {
        auto hdr =
            srp::data::Convert<srp::platform::em::ExecutionHeader>::Conv(
                payload);
        if (hdr.has_value()) {
          seen.push_back(static_cast<ara::exec::ExecutionState>(
              hdr.value().execution_state));
        }
        return true;
      });

  const char* argv[] = {"/tmp/em_test/bin/app"};
  ara::exec::AdaptiveLifecycleMenager::CreateAdaptiveLifecycleMenager<DummyApp>(
      1, argv, true);
  std::thread stopper([]() {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!DummyApp::ran.load() &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ara::exec::AdaptiveLifecycleMenager::StopAdaptiveLifecycleMenager(15);
  });
  EXPECT_EQ(0, ara::exec::AdaptiveLifecycleMenager::StartAdaptiveLifecycleMenager());
  stopper.join();
  EXPECT_TRUE(DummyApp::ran.load());
  ASSERT_GE(seen.size(), 4U);
  EXPECT_EQ(ara::exec::ExecutionState::kIdle, seen[0]);
  EXPECT_EQ(ara::exec::ExecutionState::kStarting, seen[1]);
  EXPECT_EQ(ara::exec::ExecutionState::kRunning, seen[2]);
  EXPECT_EQ(ara::exec::ExecutionState::kTerminating, seen.back());
}

}  // namespace
