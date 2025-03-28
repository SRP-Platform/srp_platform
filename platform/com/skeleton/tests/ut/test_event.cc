/**
 * @file test_event.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-14
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
#include "platform/com/skeleton/event.h"
#include "platform/com/skeleton/skeleton_packet_interpreter.h"
#include "platform/com/skeleton/tests/mocks/skeleton_com_wrapper_mock.h"

class TestEventFixture : public ::testing::Test {
 protected:
  std::shared_ptr<::testing::StrictMock<
      platform::com::skeleton::wrapper::SkeletonComWrapperMock>>
      com_mock_;
  const platform::com::InstanceIdentifierContainer container_{
      platform::com::model::ServiceModel{0x01, 0x02},
      {{"example_method", platform::com::model::EndpointModel{0x15}}}};

 public:
  TestEventFixture(/* args */)
      : com_mock_{std::make_shared<::testing::StrictMock<
            platform::com::skeleton::wrapper::SkeletonComWrapperMock>>()} {}
  ~TestEventFixture() = default;
};

TEST_F(TestEventFixture, BaseInit) {
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_)).Times(1);
  platform::com::skeleton::interpreter::SkeletonPacketInterpreter
      packet_interpreter{container_, com_mock_};
  const auto lambda = [&packet_interpreter]() {
    platform::com::skeleton::Event<uint8_t> example_method{"example_method",
                                                           packet_interpreter};
  };
  platform::com::IpcMsg result{platform::com::IpcMsgType::kObjInit,
                               0x01,
                               0x02,
                               0x15,
                               0x00,
                               0x00,
                               0x01,
                               {0x01}};
  EXPECT_CALL(*com_mock_.get(), TransmitFrame(result))
      .Times(1)
      .WillOnce(::testing::Return(true));
  EXPECT_NO_FATAL_FAILURE(lambda());
}
