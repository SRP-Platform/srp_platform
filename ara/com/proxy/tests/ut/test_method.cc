/**
 * @file test_method.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-16
 *
 * @copyright Copyright (c) 2025
 *
 */
#include <gtest/gtest.h>

#include <chrono>  // NOLINT
#include <future>  // NOLINT
#include <memory>
#include <thread>  // NOLINT

#include "ara/com/instance_identifier_container.h"
#include "ara/com/proxy/impl/method.h"
#include "ara/com/proxy/proxy_packet_interpreter.h"
#include "ara/com/proxy/tests/mocks/proxy_com_wrapper_mock.h"

class TestMethodFixture : public ::testing::Test {
 protected:
  std::shared_ptr<
      ::testing::StrictMock<ara::com::proxy::wrapper::ProxyComWrapperMock>>
      com_mock_;
  const ara::com::InstanceIdentifierContainer container_{
      ara::com::model::ServiceModel{0x01, 0x02, 0x03, 0x04},
      {{"example_method", ara::com::model::EndpointModel{0x15}}}};

 public:
  TestMethodFixture(/* args */)
      : com_mock_{std::make_shared<::testing::StrictMock<
            ara::com::proxy::wrapper::ProxyComWrapperMock>>()} {}
  ~TestMethodFixture() = default;
};

TEST_F(TestMethodFixture, BaseInit) {
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_)).Times(1);
  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};
  const auto lambda = [&packet_interpreter]() {
    ara::com::proxy::MethodImpl<uint8_t, uint8_t> example_method{
        "example_method", packet_interpreter};
  };
  EXPECT_NO_FATAL_FAILURE(lambda());
}

TEST_F(TestMethodFixture, Call) {
  const ara::com::IpcMsg expect_frame{ara::com::IpcMsgType::kRequest,
                                      0x01,
                                      0x02,
                                      0x15,
                                      0x00,
                                      0x00,
                                      0x01,
                                      {0x01}};
  ara::com::proxy::wrapper::IProxyComWrapper::ProceedFrameCallback callback_;
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce([&callback_](const ara::com::proxy::wrapper::IProxyComWrapper::
                                 ProceedFrameCallback&& c) {
        callback_ = std::move(c);
      });
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(expect_frame))
      .WillOnce(::testing::Return(true));

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::MethodImpl<uint8_t, uint8_t> example_method{
      "example_method", packet_interpreter};
  auto res_f = std::async([&example_method] { return example_method(0x01); });
  {
    const ara::com::IpcMsg response_frame{ara::com::IpcMsgType::kResponse,
                                          0x01,
                                          0x02,
                                          0x15,
                                          0x00,
                                          0x00,
                                          0x01,
                                          {0x01}};
    callback_(std::move(response_frame));
  }
  res_f.wait();
  const auto res = res_f.get();
  EXPECT_TRUE(res.HasValue());
  EXPECT_EQ(0x01, res.Value());
}

TEST_F(TestMethodFixture, CallTimeout) {
  const ara::com::IpcMsg expect_frame{ara::com::IpcMsgType::kRequest,
                                      0x01,
                                      0x02,
                                      0x15,
                                      0x00,
                                      0x00,
                                      0x01,
                                      {0x01}};

  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_)).Times(1);
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(expect_frame))
      .WillOnce(::testing::Return(true));

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::MethodImpl<uint8_t, uint8_t> example_method{
      "example_method", packet_interpreter};
  const auto res = example_method(0x01);
  EXPECT_FALSE(res.HasValue());
  EXPECT_EQ(0x06, res.Error().Value());
}

TEST_F(TestMethodFixture, CallSendingError) {
  const ara::com::IpcMsg expect_frame{ara::com::IpcMsgType::kRequest,
                                      0x01,
                                      0x02,
                                      0x15,
                                      0x00,
                                      0x00,
                                      0x01,
                                      {0x01}};

  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_)).Times(1);
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(expect_frame))
      .WillOnce(::testing::Return(false));

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::MethodImpl<uint8_t, uint8_t> example_method{
      "example_method", packet_interpreter};
  const auto res = example_method(0x01);
  EXPECT_FALSE(res.HasValue());
  EXPECT_EQ(ara::com::internal::MessageErrorDomain::Errc::kENotReachable,
            res.Error().Value());
}
TEST_F(TestMethodFixture, CallError) {
  const ara::com::IpcMsg expect_frame{ara::com::IpcMsgType::kRequest,
                                      0x01,
                                      0x02,
                                      0x15,
                                      0x00,
                                      0x00,
                                      0x01,
                                      {0x01}};
  ara::com::proxy::wrapper::IProxyComWrapper::ProceedFrameCallback callback_;
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce([&callback_](const ara::com::proxy::wrapper::IProxyComWrapper::
                                 ProceedFrameCallback&& c) {
        callback_ = std::move(c);
      });
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(expect_frame))
      .WillOnce(::testing::Return(true));

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::MethodImpl<uint8_t, uint8_t> example_method{
      "example_method", packet_interpreter};
  auto res_f = std::async([&example_method] { return example_method(0x01); });
  {
    const ara::com::IpcMsg response_frame{
        ara::com::IpcMsgType::kError,
        0x01,
        0x02,
        0x15,
        0x00,
        0x00,
        0x01,
        {ara::com::internal::MessageErrorDomain::Errc::kENotReachable}};
    callback_(std::move(response_frame));
  }
  res_f.wait();
  const auto res = res_f.get();
  EXPECT_FALSE(res.HasValue());
  EXPECT_EQ(ara::com::internal::MessageErrorDomain::Errc::kENotReachable,
            res.Error().Value());
}

class TestMethodNoReturnAckFixture : public ::testing::Test {
 protected:
  std::shared_ptr<
      ::testing::StrictMock<ara::com::proxy::wrapper::ProxyComWrapperMock>>
      com_mock_;
  const ara::com::InstanceIdentifierContainer container_{
      ara::com::model::ServiceModel{0x01, 0x02, 0x03, 0x04},
      {{"example_method",
        ara::com::model::EndpointModel{
            0x15, ara::com::model::EndpointMode::kNoReturnWithAck}}}};

 public:
  TestMethodNoReturnAckFixture(/* args */)
      : com_mock_{std::make_shared<::testing::StrictMock<
            ara::com::proxy::wrapper::ProxyComWrapperMock>>()} {}
  ~TestMethodNoReturnAckFixture() = default;
};

TEST_F(TestMethodNoReturnAckFixture, CallNoReturnWithAck) {
  const ara::com::IpcMsg expect_frame{ara::com::IpcMsgType::kRequestNoReturnAck,
                                      0x01,
                                      0x02,
                                      0x15,
                                      0x00,
                                      0x00,
                                      0x01,
                                      {0x01}};
  ara::com::proxy::wrapper::IProxyComWrapper::ProceedFrameCallback callback_;
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce([&callback_](const ara::com::proxy::wrapper::IProxyComWrapper::
                                 ProceedFrameCallback&& c) {
        callback_ = std::move(c);
      });
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(expect_frame))
      .WillOnce(::testing::Return(true));

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::MethodImpl<void, uint8_t> example_method{"example_method",
                                                            packet_interpreter};
  auto res_f = std::async([&example_method] { return example_method(0x01); });
  {
    const ara::com::IpcMsg response_frame{ara::com::IpcMsgType::kRequestAck,
                                          0x01,
                                          0x02,
                                          0x15,
                                          0x00,
                                          0x00,
                                          0x00,
                                          {}};
    callback_(std::move(response_frame));
  }
  res_f.wait();
  const auto res = res_f.get();
  EXPECT_TRUE(res.HasValue());
}

class TestMethodNoReturnWithoutAckFixture : public ::testing::Test {
 protected:
  std::shared_ptr<
      ::testing::StrictMock<ara::com::proxy::wrapper::ProxyComWrapperMock>>
      com_mock_;
  const ara::com::InstanceIdentifierContainer container_{
      ara::com::model::ServiceModel{0x01, 0x02, 0x03, 0x04},
      {{"example_method",
        ara::com::model::EndpointModel{
            0x15, ara::com::model::EndpointMode::kNoReturn}}}};

 public:
  TestMethodNoReturnWithoutAckFixture(/* args */)
      : com_mock_{std::make_shared<::testing::StrictMock<
            ara::com::proxy::wrapper::ProxyComWrapperMock>>()} {}
  ~TestMethodNoReturnWithoutAckFixture() = default;
};

TEST_F(TestMethodNoReturnWithoutAckFixture, CallNoReturnWitoutAck) {
  const ara::com::IpcMsg expect_frame{ara::com::IpcMsgType::kRequestNoReturn,
                                      0x01,
                                      0x02,
                                      0x15,
                                      0x00,
                                      0x00,
                                      0x01,
                                      {0x01}};
  ara::com::proxy::wrapper::IProxyComWrapper::ProceedFrameCallback callback_;
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce([&callback_](const ara::com::proxy::wrapper::IProxyComWrapper::
                                 ProceedFrameCallback&& c) {
        callback_ = std::move(c);
      });
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(expect_frame))
      .WillOnce(::testing::Return(true));

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::MethodImpl<void, uint8_t> example_method{"example_method",
                                                            packet_interpreter};
  auto res_f = std::async([&example_method] { return example_method(0x01); });
  res_f.wait();
  const auto res = res_f.get();
  EXPECT_TRUE(res.HasValue());
}

TEST_F(TestMethodNoReturnWithoutAckFixture, CallNoReturnWitoutAckSendingError) {
  const ara::com::IpcMsg expect_frame{ara::com::IpcMsgType::kRequestNoReturn,
                                      0x01,
                                      0x02,
                                      0x15,
                                      0x00,
                                      0x00,
                                      0x01,
                                      {0x01}};
  ara::com::proxy::wrapper::IProxyComWrapper::ProceedFrameCallback callback_;
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce([&callback_](const ara::com::proxy::wrapper::IProxyComWrapper::
                                 ProceedFrameCallback&& c) {
        callback_ = std::move(c);
      });
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(expect_frame))
      .WillOnce(::testing::Return(false));

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::MethodImpl<void, uint8_t> example_method{"example_method",
                                                            packet_interpreter};
  auto res_f = std::async([&example_method] { return example_method(0x01); });
  res_f.wait();
  const auto res = res_f.get();
  EXPECT_FALSE(res.HasValue());
  EXPECT_EQ(ara::com::internal::MessageErrorDomain::Errc::kENotReachable,
            res.Error().Value());
}
