/**
 * @file test_core_error_domain.cc
 * @author Mateusz Krajewski (matikrajek42@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-30
 *
 * @copyright Copyright (c) 2024
 *
 */
#include <gtest/gtest.h>

#include "platform/core/core_error_domain.h"  // Zawiera deklplatformcje klasy CoreErrorDomain i funkcji MakeErrorCode

class CoreErrorDomainTest : public ::testing::Test {
 protected:
  platform::core::CoreErrorDomain error_domain;
};

TEST_F(CoreErrorDomainTest, Message_ReturnsCorrectMessage) {
  EXPECT_EQ(error_domain.Message(static_cast<platform::core::ErrorDomain::CodeType>(
                platform::core::CoreErrc::kInvalidArgument)),
            std::string{"Invalid Argument"});
  EXPECT_EQ(error_domain.Message(static_cast<platform::core::ErrorDomain::CodeType>(
                platform::core::CoreErrc::kInvalidMetaModelShortname)),
            std::string{"Invalid Meta Model Shortname"});
  EXPECT_EQ(error_domain.Message(static_cast<platform::core::ErrorDomain::CodeType>(
                platform::core::CoreErrc::kInvalidMetaModelPath)),
            std::string{"Invalid Meta Model Path"});
}
TEST(MakeErrorCodeTest, MakeErrorCode_CreatesCorrectErrorCode) {
  platform::core::ErrorDomain::SupportDataType data = "123";
  platform::core::ErrorCode error_code =
      platform::core::MakeErrorCode(platform::core::CoreErrc::kInvalidArgument, data);
  EXPECT_EQ(error_code.Value(), static_cast<platform::core::ErrorDomain::CodeType>(
                                    platform::core::CoreErrc::kInvalidArgument));
  EXPECT_EQ(error_code.SupportData(), "123");
}

class TestErrorDomain : public platform::core::ErrorDomain {
 public:
  explicit TestErrorDomain(int code) : ErrorDomain(code) {}
  const std::string Name() const noexcept override {
    return {"TestErrorDomain"};
  }
  const std::string Message(CodeType error_code) const noexcept override {
    if (error_code == 12) return "Error 12 occurred";
    if (error_code == 11) return "Error 11 occurred";
    return {"Unknown error"};
  }
};

TEST(MakeErrorCodeTest, TestEQOperator) {
  TestErrorDomain domain1(12);
  TestErrorDomain domain2(11);
  EXPECT_FALSE(domain1 == domain2);
  EXPECT_TRUE(domain1 != domain2);
  EXPECT_EQ(domain1.Id(), 12);
  EXPECT_EQ(domain2.Id(), 11);
}
