/**
 * @file test_link_transport_registry.cc
 * @copyright Copyright (c) 2024
 */
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>  // NOLINT
#include <memory>
#include <thread>  // NOLINT

#include "ara/com/mavlink/link_transport_registry.h"
#include "ara/com/mavlink/testing/memory_transport.h"
#include "ara/com/mavlink/transport_error.h"
#include "ara/core/model/mavlink_model_item.h"
#include "ara/core/model_db.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using testing::MemoryTransport;

ara::core::Result<void> AcceptLoraOnly(const std::string& name) {
  if (name == "lora") {
    return {};
  }
  return MakeTransportError(TransportErrc::kUnknownLink, name);
}

std::shared_ptr<MemoryTransport> NewTransport(const std::string& n = "t") {
  return MemoryTransport::MakePair(n, n + "-peer").first;
}

TEST(LinkTransportRegistry, RegisterKnownLink) {
  LinkTransportRegistry r{AcceptLoraOnly};
  auto t = NewTransport();
  EXPECT_TRUE(r.Register("lora", t).HasValue());
  EXPECT_EQ(r.Get("lora"), t);
}

TEST(LinkTransportRegistry, RejectsNullptr) {
  LinkTransportRegistry r{AcceptLoraOnly};
  const auto res = r.Register("lora", nullptr);
  ASSERT_FALSE(res.HasValue());
  EXPECT_TRUE(IsTransportError(res.Error(), TransportErrc::kInvalidArgument));
}

TEST(LinkTransportRegistry, RejectsUnknownName) {
  LinkTransportRegistry r{AcceptLoraOnly};
  const auto res = r.Register("nieznane", NewTransport());
  ASSERT_FALSE(res.HasValue());
  EXPECT_TRUE(IsTransportError(res.Error(), TransportErrc::kUnknownLink));
  EXPECT_EQ(r.Get("nieznane"), nullptr);
}

TEST(LinkTransportRegistry, RejectsDuplicate) {
  LinkTransportRegistry r{AcceptLoraOnly};
  ASSERT_TRUE(r.Register("lora", NewTransport()).HasValue());
  const auto res = r.Register("lora", NewTransport("second"));
  ASSERT_FALSE(res.HasValue());
  EXPECT_TRUE(IsTransportError(res.Error(), TransportErrc::kAlreadyRegistered));
}

TEST(LinkTransportRegistry, UnregisterClosesTransportAndCallsHook) {
  LinkTransportRegistry r{AcceptLoraOnly};
  auto t = NewTransport();
  ASSERT_TRUE(t->Open({}).HasValue());
  ASSERT_TRUE(r.Register("lora", t).HasValue());
  std::string hooked;
  r.SetUnregisterHook([&](const std::string& n) { hooked = n; });
  ASSERT_TRUE(r.Unregister("lora").HasValue());
  EXPECT_FALSE(t->is_open());
  EXPECT_EQ(hooked, "lora");
  EXPECT_EQ(r.Get("lora"), nullptr);
}

TEST(LinkTransportRegistry, UnregisterUnknownIsError) {
  LinkTransportRegistry r{AcceptLoraOnly};
  const auto res = r.Unregister("lora");
  ASSERT_FALSE(res.HasValue());
  EXPECT_TRUE(IsTransportError(res.Error(), TransportErrc::kUnknownLink));
}

TEST(LinkTransportRegistry, ReRegisterAfterUnregister) {
  LinkTransportRegistry r{AcceptLoraOnly};
  ASSERT_TRUE(r.Register("lora", NewTransport()).HasValue());
  ASSERT_TRUE(r.Unregister("lora").HasValue());
  EXPECT_TRUE(r.Register("lora", NewTransport("again")).HasValue());
}

TEST(LinkTransportRegistry, WaitForReturnsAfterRegistration) {
  LinkTransportRegistry r{AcceptLoraOnly};
  auto t = NewTransport();
  std::thread late{[&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(r.Register("lora", t).HasValue());
  }};
  const auto t0 = std::chrono::steady_clock::now();
  const auto got = r.WaitFor("lora", std::chrono::milliseconds(5000), {});
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
  late.join();
  EXPECT_EQ(got, t);
  EXPECT_GE(ms, 100);
  EXPECT_LT(ms, 2000);
}

TEST(LinkTransportRegistry, WaitForTimesOut) {
  LinkTransportRegistry r{AcceptLoraOnly};
  EXPECT_EQ(r.WaitFor("lora", std::chrono::milliseconds(80), {}), nullptr);
}

TEST(LinkTransportRegistry, WaitForIsInterruptibleByStopToken) {
  LinkTransportRegistry r{AcceptLoraOnly};
  std::stop_source src;
  std::atomic<bool> done{false};
  std::thread w{[&] {
    EXPECT_EQ(r.WaitFor("lora", std::chrono::milliseconds(20000), src.get_token()),
              nullptr);
    done = true;
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_FALSE(done.load());
  src.request_stop();
  w.join();
  EXPECT_TRUE(done.load());
}

TEST(LinkTransportRegistry, ClearDropsEntriesWithoutClosing) {
  LinkTransportRegistry r{AcceptLoraOnly};
  auto t = NewTransport();
  ASSERT_TRUE(t->Open({}).HasValue());
  ASSERT_TRUE(r.Register("lora", t).HasValue());
  r.Clear();
  EXPECT_EQ(r.Get("lora"), nullptr);
  EXPECT_TRUE(t->is_open());
}

TEST(LinkTransportRegistry, RegistryKeepsTransportAlive) {
  LinkTransportRegistry r{AcceptLoraOnly};
  std::weak_ptr<MemoryTransport> weak;
  {
    auto t = NewTransport();
    weak = t;
    ASSERT_TRUE(r.Register("lora", t).HasValue());
  }
  EXPECT_FALSE(weak.expired());
  r.Clear();
  EXPECT_TRUE(weak.expired());
}

// --- rejestr globalny: walidacja względem ModelDataBase ---

void AddModelLink(const std::string& item_name, const std::string& link_name) {
  ara::core::model::ModelMavlinkLink l;
  l.name = link_name;
  const auto m = ara::core::model::ModelMavlink::CreatSkeletonItem(
      "com" + item_name, 1, 1, 0, 1, 1, 0, 0, "dialect", {l});
  ASSERT_TRUE(ara::core::ModelDataBase::GetInstance()
                  .AddNewItem(item_name, {m.Value()})
                  .HasValue());
}

TEST(GlobalLinkTransportRegistry, ValidatesAgainstModel) {
  AddModelLink("app/reg_ext", "reg_lora");

  // nieznane łącze
  const auto r1 = RegisterLinkTransport("reg_brak", NewTransport());
  ASSERT_FALSE(r1.HasValue());
  EXPECT_TRUE(IsTransportError(r1.Error(), TransportErrc::kUnknownLink));
  // nullptr
  const auto r3 = RegisterLinkTransport("reg_lora", nullptr);
  ASSERT_FALSE(r3.HasValue());
  EXPECT_TRUE(IsTransportError(r3.Error(), TransportErrc::kInvalidArgument));
  // poprawnie + duplikat + wyrejestrowanie
  EXPECT_TRUE(RegisterLinkTransport("reg_lora", NewTransport()).HasValue());
  const auto r4 = RegisterLinkTransport("reg_lora", NewTransport("dup"));
  ASSERT_FALSE(r4.HasValue());
  EXPECT_TRUE(IsTransportError(r4.Error(), TransportErrc::kAlreadyRegistered));
  EXPECT_TRUE(UnregisterLinkTransport("reg_lora").HasValue());
  ClearLinkTransports();
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
