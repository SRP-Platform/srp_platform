/**
 * @file test_method.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-13
 *
 * @copyright Copyright (c) 2025
 *
 */

#include <gtest/gtest.h>

#include <chrono>  // NOLINT
#include <future>  // NOLINT
#include <memory>
#include <thread>  // NOLINT

#include "platform/com/instance_identifier_container.h"
#include "platform/com/skeleton/impl/method.h"
#include "platform/com/skeleton/skeleton_packet_interpreter.h"
#include "platform/com/skeleton/tests/mocks/skeleton_com_wrapper_mock.h"

class TestMethodFixture : public ::testing::Test {
 protected:
  std::shared_ptr<::testing::StrictMock<
      platform::com::skeleton::wrapper::SkeletonComWrapperMock>>
      com_mock_;
  const platform::com::InstanceIdentifierContainer container_{
      platform::com::model::ServiceModel{0x01, 0x02},
      {{"example_method", platform::com::model::EndpointModel{0x15}}}};

 public:
  TestMethodFixture(/* args */)
      : com_mock_{std::make_shared<::testing::StrictMock<
            platform::com::skeleton::wrapper::SkeletonComWrapperMock>>()} {}
  ~TestMethodFixture() = default;
};

TEST_F(TestMethodFixture, BaseInit) {
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_)).Times(1);
  platform::com::skeleton::interpreter::SkeletonPacketInterpreter
      packet_interpreter{container_, com_mock_};
  const auto lambda = [&packet_interpreter]() {
    platform::com::skeleton::MethodImpl<uint8_t, uint8_t> example_method{
        "example_method", packet_interpreter};
  };
  EXPECT_NO_FATAL_FAILURE(lambda());
}

TEST_F(TestMethodFixture, CreateMethodWithoutBindMethod) {
  platform::com::skeleton::wrapper::ISkeletonComWrapper::ProceedFrameCallback
      callback_;
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce(
          [&callback_](const platform::com::skeleton::wrapper::
                           ISkeletonComWrapper::ProceedFrameCallback&& c) {
            callback_ = std::move(c);
          });
  platform::com::skeleton::interpreter::SkeletonPacketInterpreter
      packet_interpreter{container_, com_mock_};
  platform::com::skeleton::MethodImpl<uint8_t, uint8_t> example_method{
      "example_method", packet_interpreter};
  platform::com::IpcMsg result{
      platform::com::IpcMsgType::kError,
      0x01,
      0x02,
      0x15,
      0x11223344,
      0x0112,
      0x01,
      {platform::com::internal::MessageErrorCode::kEGeneralError}};
  std::promise<int> p;
  std::future<int> f = p.get_future();
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(result))
      .Times(1)
      .WillOnce([&p](const platform::com::IpcMsg&) {
        p.set_value(1);
        return true;
      });
  {
    platform::com::IpcMsg request{platform::com::IpcMsgType::kRequest,
                                  0x01,
                                  0x02,
                                  0x15,
                                  0x11223344,
                                  0x0112,
                                  0x01,
                                  {0x01}};
    EXPECT_NO_FATAL_FAILURE(callback_(std::move(request)));
  }
  EXPECT_EQ(std::future_status::ready, f.wait_for(std::chrono::seconds{10}));
}

TEST_F(TestMethodFixture, CreateMethodWithBindMethod) {
  platform::com::skeleton::wrapper::ISkeletonComWrapper::ProceedFrameCallback
      callback_;
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce(
          [&callback_](const platform::com::skeleton::wrapper::
                           ISkeletonComWrapper::ProceedFrameCallback&& c) {
            callback_ = std::move(c);
          });
  platform::com::skeleton::interpreter::SkeletonPacketInterpreter
      packet_interpreter{container_, com_mock_};
  platform::com::skeleton::MethodImpl<uint8_t, uint8_t> example_method{
      "example_method", packet_interpreter,
      [](const uint8_t& data) { return data + 0x11U; }};
  platform::com::IpcMsg result{platform::com::IpcMsgType::kResponse,
                               0x01,
                               0x02,
                               0x15,
                               0x11223344,
                               0x0112,
                               0x01,
                               {0x12}};
  std::promise<int> p;
  std::future<int> f = p.get_future();
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(result))
      .Times(1)
      .WillOnce([&p](const platform::com::IpcMsg&) {
        p.set_value(1);
        return true;
      });
  {
    platform::com::IpcMsg request{platform::com::IpcMsgType::kRequest,
                                  0x01,
                                  0x02,
                                  0x15,
                                  0x11223344,
                                  0x0112,
                                  0x01,
                                  {0x01}};
    EXPECT_NO_FATAL_FAILURE(callback_(std::move(request)));
  }
  EXPECT_EQ(std::future_status::ready, f.wait_for(std::chrono::seconds{10}));
}

TEST_F(TestMethodFixture, CreateMethodWithoutBindMethodSetMethod) {
  platform::com::skeleton::wrapper::ISkeletonComWrapper::ProceedFrameCallback
      callback_;
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce(
          [&callback_](const platform::com::skeleton::wrapper::
                           ISkeletonComWrapper::ProceedFrameCallback&& c) {
            callback_ = std::move(c);
          });
  platform::com::skeleton::interpreter::SkeletonPacketInterpreter
      packet_interpreter{container_, com_mock_};
  platform::com::skeleton::MethodImpl<uint8_t, uint8_t> example_method{
      "example_method", packet_interpreter};
  {
    platform::com::IpcMsg result{
        platform::com::IpcMsgType::kError,
        0x01,
        0x02,
        0x15,
        0x11223344,
        0x0112,
        0x01,
        {platform::com::internal::MessageErrorCode::kEGeneralError}};
    std::promise<int> p;
    std::future<int> f = p.get_future();
    EXPECT_CALL(*com_mock_.get(), TransmitFrame(result))
        .Times(1)
        .WillOnce([&p](const platform::com::IpcMsg&) {
          p.set_value(1);
          return true;
        });
    {
      platform::com::IpcMsg request{platform::com::IpcMsgType::kRequest,
                                    0x01,
                                    0x02,
                                    0x15,
                                    0x11223344,
                                    0x0112,
                                    0x01,
                                    {0x01}};
      EXPECT_NO_FATAL_FAILURE(callback_(std::move(request)));
    }
    EXPECT_EQ(std::future_status::ready, f.wait_for(std::chrono::seconds{10}));
  }
  example_method.Bind([](const uint8_t& data) { return data + 0x11U; });
  {
    platform::com::IpcMsg result{platform::com::IpcMsgType::kResponse,
                                 0x01,
                                 0x02,
                                 0x15,
                                 0x11223344,
                                 0x0112,
                                 0x01,
                                 {0x12}};
    std::promise<int> p;
    std::future<int> f = p.get_future();
    EXPECT_CALL(*com_mock_.get(), TransmitFrame(result))
        .Times(1)
        .WillOnce([&p](const platform::com::IpcMsg&) {
          p.set_value(1);
          return true;
        });
    {
      platform::com::IpcMsg request{platform::com::IpcMsgType::kRequest,
                                    0x01,
                                    0x02,
                                    0x15,
                                    0x11223344,
                                    0x0112,
                                    0x01,
                                    {0x01}};
      EXPECT_NO_FATAL_FAILURE(callback_(std::move(request)));
    }
    EXPECT_EQ(std::future_status::ready, f.wait_for(std::chrono::seconds{10}));
  }
}

TEST_F(TestMethodFixture, CreateMethodWithBindMethodDoubleCall) {
  platform::com::skeleton::wrapper::ISkeletonComWrapper::ProceedFrameCallback
      callback_;
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce(
          [&callback_](const platform::com::skeleton::wrapper::
                           ISkeletonComWrapper::ProceedFrameCallback&& c) {
            callback_ = std::move(c);
          });
  platform::com::skeleton::interpreter::SkeletonPacketInterpreter
      packet_interpreter{container_, com_mock_};
  platform::com::skeleton::MethodImpl<uint8_t, uint8_t> example_method{
      "example_method", packet_interpreter, [](const uint8_t& data) {
        std::this_thread::sleep_for(std::chrono::seconds{3});
        return data + 0x11U;
      }};
  platform::com::IpcMsg result{platform::com::IpcMsgType::kResponse,
                               0x01,
                               0x02,
                               0x15,
                               0x11223344,
                               0x0112,
                               0x01,
                               {0x12}};
  uint8_t a{0U};
  std::promise<int> p;
  std::future<int> f = p.get_future();
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(result))
      .Times(2)
      .WillRepeatedly([&p, &a](const platform::com::IpcMsg&) {
        if (a++ > 0) {
          p.set_value(1);
        }
        return true;
      });
  {
    platform::com::IpcMsg request{platform::com::IpcMsgType::kRequest,
                                  0x01,
                                  0x02,
                                  0x15,
                                  0x11223344,
                                  0x0112,
                                  0x01,
                                  {0x01}};
    EXPECT_NO_FATAL_FAILURE(callback_(std::move(request)));
    EXPECT_NO_FATAL_FAILURE(callback_(std::move(request)));
  }
  EXPECT_EQ(std::future_status::ready, f.wait_for(std::chrono::seconds{10}));
}

TEST_F(TestMethodFixture, CreateMethodWithBindMethodTimeOut) {
  platform::com::skeleton::wrapper::ISkeletonComWrapper::ProceedFrameCallback
      callback_;
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce(
          [&callback_](const platform::com::skeleton::wrapper::
                           ISkeletonComWrapper::ProceedFrameCallback&& c) {
            callback_ = std::move(c);
          });
  platform::com::skeleton::interpreter::SkeletonPacketInterpreter
      packet_interpreter{container_, com_mock_};
  platform::com::skeleton::MethodImpl<uint8_t, uint8_t> example_method{
      "example_method", packet_interpreter, [](const uint8_t& data) {
        std::this_thread::sleep_for(std::chrono::seconds{11});
        return data + 0x11U;
      }};
  platform::com::IpcMsg result{platform::com::IpcMsgType::kResponse,
                               0x01,
                               0x02,
                               0x15,
                               0x11223344,
                               0x0112,
                               0x01,
                               {0x12}};
  std::promise<int> p;
  std::future<int> f = p.get_future();
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(result))
      .Times(1)
      .WillRepeatedly([&p](const platform::com::IpcMsg&) {
        p.set_value(1);
        return true;
      });

  platform::com::IpcMsg result2{
      platform::com::IpcMsgType::kError,
      0x01,
      0x02,
      0x15,
      0x11223344,
      0x0112,
      0x01,
      {platform::com::internal::MessageErrorCode::kENotReady}};
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(result2))
      .Times(1)
      .WillRepeatedly(::testing::Return(true));
  {
    platform::com::IpcMsg request{platform::com::IpcMsgType::kRequest,
                                  0x01,
                                  0x02,
                                  0x15,
                                  0x11223344,
                                  0x0112,
                                  0x01,
                                  {0x01}};
    EXPECT_NO_FATAL_FAILURE(callback_(std::move(request)));
    EXPECT_NO_FATAL_FAILURE(callback_(std::move(request)));
  }
  EXPECT_EQ(std::future_status::ready, f.wait_for(std::chrono::seconds{10}));
}
