/**
 * @file test_fields.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-16
 *
 * @copyright Copyright (c) 2025
 *
 */
#include <gtest/gtest.h>

#include <memory>

#include "platform/com/instance_identifier_container.h"
#include "platform/com/proxy/impl/fields.h"
#include "platform/com/proxy/proxy_packet_interpreter.h"
#include "platform/com/proxy/tests/mocks/proxy_com_wrapper_mock.h"

class TestFieldFixtureGet : public ::testing::Test {
 protected:
  std::shared_ptr<
      ::testing::StrictMock<platform::com::proxy::wrapper::ProxyComWrapperMock>>
      com_mock_;
  const platform::com::InstanceIdentifierContainer container_{
      platform::com::model::ServiceModel{0x01, 0x02},
      {{"example_event", platform::com::model::EndpointModel{0x15}},
       {"example_event/get", platform::com::model::EndpointModel{0x14}}}};

 public:
  TestFieldFixtureGet(/* args */)
      : com_mock_{std::make_shared<::testing::StrictMock<
            platform::com::proxy::wrapper::ProxyComWrapperMock>>()} {}
  ~TestFieldFixtureGet() = default;
};

TEST_F(TestFieldFixtureGet, BaseInitWithSet) {
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_)).Times(1);
  platform::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};
  const auto lambda = [&packet_interpreter]() {
    platform::com::proxy::FieldsImpl<uint8_t> ExampleEvent{"example_event",
                                                      packet_interpreter};
  };
  EXPECT_NO_FATAL_FAILURE(lambda());
}

class TestFieldFixtureSetGet : public ::testing::Test {
 protected:
  std::shared_ptr<
      ::testing::StrictMock<platform::com::proxy::wrapper::ProxyComWrapperMock>>
      com_mock_;
  const platform::com::InstanceIdentifierContainer container_{
      platform::com::model::ServiceModel{0x01, 0x02},
      {{"example_event", platform::com::model::EndpointModel{0x15}},
       {"example_event/get", platform::com::model::EndpointModel{0x14}},
       {"example_event/set", platform::com::model::EndpointModel{
                                 0x12, platform::com::model::kNoReturnWithAck}}}};

 public:
  TestFieldFixtureSetGet(/* args */)
      : com_mock_{std::make_shared<::testing::StrictMock<
            platform::com::proxy::wrapper::ProxyComWrapperMock>>()} {}
  ~TestFieldFixtureSetGet() = default;
};

TEST_F(TestFieldFixtureSetGet, BaseInitWithSet) {
  EXPECT_CALL(*com_mock_.get(), SetProceedFrameCallback(::testing::_)).Times(1);
  platform::com::proxy::interpreter::ProxyPacketInterpreter packet_interpreter{
      container_, com_mock_};
  const auto lambda = [&packet_interpreter]() {
    platform::com::proxy::FieldsImpl<uint8_t, true> ExampleEvent{"example_event",
                                                            packet_interpreter};
  };
  EXPECT_NO_FATAL_FAILURE(lambda());
}
