/**
 * @file test_transport_error.cc
 * @copyright Copyright (c) 2024
 */
#include <gtest/gtest.h>

#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {

TEST(TransportError, CodesCarryDomainAndData) {
  const auto e = MakeTransportError(TransportErrc::kPersistent, "port zajęty");
  EXPECT_TRUE(IsTransportError(e, TransportErrc::kPersistent));
  EXPECT_FALSE(IsTransportError(e, TransportErrc::kRetryable));
  EXPECT_EQ(e.SupportData(), "port zajęty");
  EXPECT_EQ(e.Domain().Name(), "MavlinkTransportErrorDomain");
}

TEST(TransportError, EveryCodeHasMessage) {
  for (auto c : {TransportErrc::kRetryable, TransportErrc::kPersistent,
                 TransportErrc::kClosed, TransportErrc::kUnknownLink,
                 TransportErrc::kInvalidArgument,
                 TransportErrc::kAlreadyRegistered}) {
    const auto e = MakeTransportError(c);
    EXPECT_FALSE(e.Domain().Message(e.Value()).empty());
    EXPECT_NE(e.Domain().Message(e.Value()), "Unknown transport error");
  }
}

TEST(TransportError, DifferentDomainIsNotTransportError) {
  const auto e = ara::core::ErrorCode{
      static_cast<ara::core::ErrorDomain::CodeType>(TransportErrc::kClosed),
      GetTransportErrorDomain()};
  EXPECT_TRUE(IsTransportError(e, TransportErrc::kClosed));
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
