/**
 * @file test_radio_driver_registry.cc
 * @brief Driver registry and static registration: ids registered before main(), duplicate and
 * empty ids, unknown driver, factory returning null or throwing.
 * @copyright Copyright (c) 2024
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "ara/com/mavlink/radio_driver_registry.h"
#include "ara/com/mavlink/serial_radio.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {

bool Contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

bool Has(const std::vector<std::string>& v, const std::string& id) {
  return std::find(v.begin(), v.end(), id) != v.end();
}

RadioDriverFactory NullFactory() {
  return [](const std::string&) { return std::shared_ptr<IRadioDriver>{}; };
}

}  // namespace

// Two registrations in one translation unit (the macro must produce unique object names).
SRP_MAVLINK_REGISTER_DRIVER("registry_test_x", SerialRadioFactory());
SRP_MAVLINK_REGISTER_DRIVER("registry_test_y", SerialRadioFactory());

namespace {

TEST(RadioDriverRegistry, StaticRegistrationsAreVisibleInTheGlobalRegistry) {
  const auto ids = RadioDriverRegistry::Global().Ids();
  for (const char* id : {"serial_radio", "test_serial_a", "test_serial_b", "registry_test_x", "registry_test_y"}) {
    EXPECT_TRUE(Has(ids, id)) << id;
  }
  EXPECT_TRUE(RadioDriverRegistry::Global().Errors().empty());
}

TEST(RadioDriverRegistry, CreateReturnsTheDriverOfTheRequestedId) {
  const auto d = RadioDriverRegistry::Global().Create("test_serial_b", "/nonexistent/cfg.json");
  ASSERT_TRUE(d.HasValue());
  EXPECT_EQ(d.Value()->Name(), "test_serial_b");
  const auto s = RadioDriverRegistry::Global().Create("serial_radio", "/nonexistent/cfg.json");
  ASSERT_TRUE(s.HasValue());
  EXPECT_TRUE(Contains(s.Value()->Name(), "serial_radio"));
}

TEST(RadioDriverRegistry, DuplicateIdIsAnErrorNamingTheIdAndKeepsTheFirstFactory) {
  RadioDriverRegistry r;
  ASSERT_TRUE(r.Register("radio", SerialRadioFactory()).HasValue());
  const auto again = r.Register("radio", NullFactory());
  ASSERT_FALSE(again.HasValue());
  EXPECT_TRUE(Contains(std::string(again.Error().SupportData()), "radio"));
  ASSERT_EQ(r.Errors().size(), 1U);
  EXPECT_TRUE(Contains(r.Errors()[0], "duplicate driver id 'radio'")) << r.Errors()[0];
  EXPECT_TRUE(r.Create("radio", "/nonexistent/cfg.json").HasValue());  // the first factory stayed
}

TEST(RadioDriverRegistry, RejectsEmptyIdAndNullFactory) {
  RadioDriverRegistry r;
  EXPECT_FALSE(r.Register("", SerialRadioFactory()).HasValue());
  EXPECT_FALSE(r.Register("x", RadioDriverFactory{}).HasValue());
  EXPECT_EQ(r.Errors().size(), 2U);
  EXPECT_TRUE(r.Ids().empty());
}

TEST(RadioDriverRegistry, UnknownDriverListsKnownIds) {
  RadioDriverRegistry r;
  const auto none = r.Create("x", "");
  ASSERT_FALSE(none.HasValue());
  EXPECT_TRUE(Contains(std::string(none.Error().SupportData()), "none"));
  ASSERT_TRUE(r.Register("b_radio", SerialRadioFactory()).HasValue());
  ASSERT_TRUE(r.Register("a_radio", SerialRadioFactory()).HasValue());
  const auto d = r.Create("nope", "");
  ASSERT_FALSE(d.HasValue());
  const std::string text{d.Error().SupportData()};
  EXPECT_TRUE(Contains(text, "nope")) << text;
  EXPECT_TRUE(Contains(text, "a_radio, b_radio")) << text;
}

TEST(RadioDriverRegistry, FactoryReturningNullIsReportedWithConfigPath) {
  RadioDriverRegistry r;
  ASSERT_TRUE(r.Register("null_radio", NullFactory()).HasValue());
  const auto d = r.Create("null_radio", "/etc/x.json");
  ASSERT_FALSE(d.HasValue());
  EXPECT_TRUE(Contains(std::string(d.Error().SupportData()), "/etc/x.json"));
}

TEST(RadioDriverRegistry, FactoryThrowingIsAnError) {
  RadioDriverRegistry r;
  ASSERT_TRUE(r.Register("throw_radio", [](const std::string&) -> std::shared_ptr<IRadioDriver> {
                throw std::runtime_error("boom");
              }).HasValue());
  const auto d = r.Create("throw_radio", "");
  ASSERT_FALSE(d.HasValue());
  EXPECT_TRUE(Contains(std::string(d.Error().SupportData()), "boom"));
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
