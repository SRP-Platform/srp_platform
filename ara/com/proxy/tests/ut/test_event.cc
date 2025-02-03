/**
 * @file test_event.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-02-03
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include <gtest/gtest.h>

#include <memory>

#include "ara/com/instance_identifier_container.h"
#include "ara/com/proxy/event.h"
#include "ara/com/proxy/proxy_packet_interpreter.h"
#include "ara/com/proxy/tests/mocks/proxy_com_wrapper_mock.h"

class TestEventFixture : public ::testing::Test {
 protected:
  std::shared_ptr<
      ::testing::StrictMock<ara::com::proxy::wrapper::ProxyComWrapperMock>>
      com_mock_;
  const ara::com::InstanceIdentifierContainer container_{
      ara::com::model::ServiceModel{0x01, 0x02, 0x03, 0x04},
      {{"example_event", ara::com::model::EndpointModel{0x15}}}};

 public:
  TestEventFixture(/* args */)
      : com_mock_{std::make_shared<::testing::StrictMock<
            ara::com::proxy::wrapper::ProxyComWrapperMock>>()} {}
  ~TestEventFixture() = default;
};

TEST_F(TestEventFixture, BaseInit) {
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_)).Times(1);
  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};
  const auto lambda = [&packet_interpreter]() {
    ara::com::proxy::Event<uint8_t> ExampleEvent{"example_event",
                                                 packet_interpreter};
  };
  EXPECT_NO_FATAL_FAILURE(lambda());
}

TEST_F(TestEventFixture, EventSubscriptionCall) {
  const ara::com::IpcMsg expect_frame{
      ara::com::IpcMsgType::kSubscribe, 0x01, 0x02, 0x15, 0x00, 0x00, 0x00, {}};

  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_)).Times(1);
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(expect_frame)).Times(1);

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::Event<uint8_t> ExampleEvent{"example_event",
                                               packet_interpreter};

  auto sub_flag = false;
  ExampleEvent.Subscribe([&sub_flag]() { sub_flag = true; }, 1U);
  EXPECT_FALSE(sub_flag);
}

TEST_F(TestEventFixture, EventSubscriptionACK) {
  const ara::com::IpcMsg expect_frame{
      ara::com::IpcMsgType::kSubscribe, 0x01, 0x02, 0x15, 0x00, 0x00, 0x00, {}};

  ara::com::proxy::wrapper::IProxyComWrapper::ProceedFrameCallback callback_;

  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce([&callback_](const ara::com::proxy::wrapper::IProxyComWrapper::
                                 ProceedFrameCallback&& c) {
        callback_ = std::move(c);
      });

  EXPECT_CALL(*com_mock_.get(), TransmitFrame(expect_frame)).Times(1);

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::Event<uint8_t> ExampleEvent{"example_event",
                                               packet_interpreter};

  auto sub_flag = 0;
  ExampleEvent.Subscribe([&sub_flag]() { sub_flag++; }, 1U);
  {
    const ara::com::IpcMsg ack_frame{ara::com::IpcMsgType::kSubscribeAck,
                                     0x01,
                                     0x02,
                                     0x15,
                                     0x00,
                                     0x00,
                                     0x00,
                                     {}};
    callback_(std::move(ack_frame));
  }
  {
    const ara::com::IpcMsg ack_frame{ara::com::IpcMsgType::kSubscribeAck,
                                     0x01,
                                     0x02,
                                     0x15,
                                     0x00,
                                     0x00,
                                     0x00,
                                     {}};
    callback_(std::move(ack_frame));
  }
  EXPECT_EQ(1, sub_flag);
}

TEST_F(TestEventFixture, EventStopSubscription) {
  ara::com::proxy::wrapper::IProxyComWrapper::ProceedFrameCallback callback_;

  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce([&callback_](const ara::com::proxy::wrapper::IProxyComWrapper::
                                 ProceedFrameCallback&& c) {
        callback_ = std::move(c);
      });

  EXPECT_CALL(*com_mock_.get(), TransmitFrame(::testing::_)).Times(3);

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::Event<uint8_t> ExampleEvent{"example_event",
                                               packet_interpreter};

  auto sub_flag = 0;
  ExampleEvent.Subscribe([&sub_flag]() { sub_flag++; }, 1U);
  {
    const ara::com::IpcMsg ack_frame{ara::com::IpcMsgType::kSubscribeAck,
                                     0x01,
                                     0x02,
                                     0x15,
                                     0x00,
                                     0x00,
                                     0x00,
                                     {}};
    callback_(std::move(ack_frame));
  }
  ExampleEvent.Unsubscribe();
  ExampleEvent.Subscribe([&sub_flag]() { sub_flag++; }, 1U);
  {
    const ara::com::IpcMsg ack_frame{ara::com::IpcMsgType::kSubscribeAck,
                                     0x01,
                                     0x02,
                                     0x15,
                                     0x00,
                                     0x00,
                                     0x00,
                                     {}};
    callback_(std::move(ack_frame));
  }
  EXPECT_EQ(2, sub_flag);
}

TEST_F(TestEventFixture, EventUpdate) {
  const ara::com::IpcMsg expect_frame{
      ara::com::IpcMsgType::kSubscribe, 0x01, 0x02, 0x15, 0x00, 0x00, 0x00, {}};

  ara::com::proxy::wrapper::IProxyComWrapper::ProceedFrameCallback callback_;

  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_))
      .WillOnce([&callback_](const ara::com::proxy::wrapper::IProxyComWrapper::
                                 ProceedFrameCallback&& c) {
        callback_ = std::move(c);
      });
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(expect_frame)).Times(1);

  ara::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};

  ara::com::proxy::Event<uint8_t> ExampleEvent{"example_event",
                                               packet_interpreter};

  auto sub_flag = false;
  ExampleEvent.Subscribe([&sub_flag]() { sub_flag = true; }, 1U);
  {
    const ara::com::IpcMsg ack_frame{ara::com::IpcMsgType::kSubscribeAck,
                                     0x01,
                                     0x02,
                                     0x15,
                                     0x00,
                                     0x00,
                                     0x00,
                                     {}};
    callback_(std::move(ack_frame));
  }
  EXPECT_TRUE(sub_flag);
  auto data_rx = false;
  ExampleEvent.SetReceiveHandler([&data_rx]() { data_rx = true; });

  {
    const ara::com::IpcMsg ack_frame{ara::com::IpcMsgType::kEvent,
                                     0x01,
                                     0x02,
                                     0x15,
                                     0x00,
                                     0x00,
                                     0x01,
                                     {0x10}};
    callback_(std::move(ack_frame));
  }
  EXPECT_TRUE(data_rx);
  data_rx = false;
  ExampleEvent.GetNewSamples(
      [&data_rx](auto* val) {
        data_rx = true;
        EXPECT_NE(nullptr, val);
        EXPECT_EQ(static_cast<uint8_t>(0x10), *val);
      },
      1);
  EXPECT_TRUE(data_rx);
}