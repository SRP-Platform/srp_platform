/**
 * Copyright (c) 2024
 *
 * @file test_daemon_config.cc
 * @brief MavlinkDaemon configuration: fields, defaults and readable errors.
 */
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "platform/common/mavlink_daemon/code/config/daemon_config.h"

namespace srp {
namespace mavlink_daemon {
namespace {
using json = nlohmann::ordered_json;  // the order of the links is the order of the file

TEST(DaemonConfig, ParsesAFullConfiguration) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({
    "system_id": 7, "component_id": 2,
    "socket_path": "/tmp/x.sock", "socket_mode": "0600",
    "restart_backoff": {"min_ms": 200, "max_ms": 9000},
    "links": {
      "lora": {"driver": "ebyte_e220", "driver_config": "/etc/srp/e220.json",
               "signing": {"enabled": true, "key_file": "/k", "link_id": 9, "state_file": "/s", "accept_unsigned": true}},
      "radio_b": {"driver": "serial_radio", "driver_config": "/etc/srp/radio_b.json"}
    }})"));
  ASSERT_TRUE(cfg.HasValue()) << cfg.Error().SupportData();
  const auto& c = cfg.Value();
  EXPECT_EQ(c.system_id, 7);
  EXPECT_EQ(c.component_id, 2);
  EXPECT_EQ(c.socket_path, "/tmp/x.sock");
  EXPECT_EQ(c.socket_mode, 0600U);
  EXPECT_EQ(c.restart_backoff.min.count(), 200);
  EXPECT_EQ(c.restart_backoff.max.count(), 9000);
  ASSERT_EQ(c.links.size(), 2U);
  const auto& lora = c.links[0].name == "lora" ? c.links[0] : c.links[1];
  const auto& radio_b = c.links[0].name == "lora" ? c.links[1] : c.links[0];
  EXPECT_EQ(lora.driver, "ebyte_e220");
  EXPECT_EQ(lora.driver_config, "/etc/srp/e220.json");
  EXPECT_TRUE(lora.signing_enabled);
  EXPECT_EQ(lora.key_file, "/k");
  EXPECT_EQ(lora.signing_link_id, 9);
  EXPECT_EQ(lora.state_file, "/s");
  EXPECT_TRUE(lora.accept_unsigned);
  EXPECT_EQ(radio_b.driver, "serial_radio");
  EXPECT_FALSE(radio_b.signing_enabled);
}

TEST(DaemonConfig, LinksKeepTheOrderOfTheFileNotAlphabetical) {
  // link_index of the MavlinkLinkService is the position in the file: radio_b first -> index 0.
  const auto cfg = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "links": {
    "radio_b": {"driver": "serial_radio"},
    "radio_a": {"driver": "serial_radio"},
    "aaa": {"driver": "serial_radio"}}})"));
  ASSERT_TRUE(cfg.HasValue()) << cfg.Error().SupportData();
  ASSERT_EQ(cfg.Value().links.size(), 3U);
  EXPECT_EQ(cfg.Value().links[0].name, "radio_b");
  EXPECT_EQ(cfg.Value().links[1].name, "radio_a");
  EXPECT_EQ(cfg.Value().links[2].name, "aaa");
}

TEST(DaemonConfig, StatsPeriodDefaultsToOneSecondAndIsRangeChecked) {
  const auto base = R"({"system_id": 1, "component_id": 1, "links": {}%s})";
  char buf[200];
  std::snprintf(buf, sizeof(buf), base, "");
  auto def = ParseDaemonConfig(json::parse(buf));
  ASSERT_TRUE(def.HasValue());
  EXPECT_EQ(def.Value().stats_period_ms, 1000U);
  std::snprintf(buf, sizeof(buf), base, R"(, "stats_period_ms": 250)");
  auto ok = ParseDaemonConfig(json::parse(buf));
  ASSERT_TRUE(ok.HasValue());
  EXPECT_EQ(ok.Value().stats_period_ms, 250U);
  for (const char* bad : {R"(, "stats_period_ms": 0)", R"(, "stats_period_ms": 3600001)",
                          R"(, "stats_period_ms": "fast")"}) {
    std::snprintf(buf, sizeof(buf), base, bad);
    const auto r = ParseDaemonConfig(json::parse(buf));
    ASSERT_FALSE(r.HasValue()) << bad;
    EXPECT_NE(std::string(r.Error().SupportData()).find("stats_period_ms"), std::string::npos) << bad;
  }
}

TEST(DaemonConfig, StatsPeriodPerLinkSlotMustBeAtLeast50Ms) {
  const auto cfg = [](int period, int links) {
    std::string l;
    for (int i = 0; i < links; ++i) {
      l += (i ? "," : "") + std::string("\"l") + std::to_string(i) + "\": {\"driver\": \"d\"}";
    }
    return json::parse("{\"system_id\": 1, \"component_id\": 1, \"stats_period_ms\": " + std::to_string(period) +
                       ", \"links\": {" + l + "}}");
  };
  EXPECT_TRUE(ParseDaemonConfig(cfg(100, 2)).HasValue());  // 50 ms slot: the minimum
  EXPECT_TRUE(ParseDaemonConfig(cfg(10, 0)).HasValue());   // no links: no slots
  const auto bad = ParseDaemonConfig(cfg(99, 2));          // 49 ms slot
  ASSERT_FALSE(bad.HasValue());
  EXPECT_NE(std::string(bad.Error().SupportData()).find("stats_period_ms"), std::string::npos);
  EXPECT_FALSE(ParseDaemonConfig(cfg(1000, 21)).HasValue());  // 47 ms
}

TEST(DaemonConfig, ReportsAllProblemsAtOnceNamingTheLinks) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({
    "system_id": 0, "component_id": 300,
    "links": {
      "a": {"driver": 5},
      "b": {"driver": ""},
      "c": {"driver": "x", "signing": {"enabled": true}},
      "d": 5
    }})"));
  ASSERT_FALSE(cfg.HasValue());
  const auto t = cfg.Error().SupportData();
  EXPECT_NE(t.find("system_id"), std::string::npos) << t;
  EXPECT_NE(t.find("component_id"), std::string::npos) << t;
  EXPECT_NE(t.find("link 'a'"), std::string::npos) << t;
  EXPECT_NE(t.find("link 'b'"), std::string::npos) << t;
  EXPECT_NE(t.find("link 'c'"), std::string::npos) << t;
  EXPECT_NE(t.find("driver"), std::string::npos) << t;
  EXPECT_NE(t.find("key_file"), std::string::npos) << t;
  EXPECT_NE(t.find("link 'd'"), std::string::npos) << t;
}

TEST(DaemonConfig, MissingRequiredFieldsAreReported) {
  const auto cfg = ParseDaemonConfig(json::parse("{}"));
  ASSERT_FALSE(cfg.HasValue());
  const auto t = cfg.Error().SupportData();
  EXPECT_NE(t.find("system_id"), std::string::npos);
  EXPECT_NE(t.find("component_id"), std::string::npos);
  EXPECT_NE(t.find("links"), std::string::npos);
  EXPECT_FALSE(ParseDaemonConfig(json::parse("[1]")).HasValue());
}

TEST(DaemonConfig, UnknownKeyIsAStartupErrorNamingTheKeyAndTheLink) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({
    "system_id": 1, "component_id": 1,
    "links": {"radio_a": {"driver": "serial_radio", "baud": 9600, "heartbeat_Hz": 1,
                          "signing": {"enabled": false, "keyfile": "/k"}}}})"));
  ASSERT_FALSE(cfg.HasValue());
  const auto t = cfg.Error().SupportData();
  EXPECT_NE(t.find("link 'radio_a': unknown key 'heartbeat_Hz'"), std::string::npos) << t;
  EXPECT_NE(t.find("link 'radio_a': unknown key 'baud'"), std::string::npos) << t;
  EXPECT_NE(t.find("link 'radio_a' signing: unknown key 'keyfile'"), std::string::npos) << t;
}

TEST(DaemonConfig, RemovedKeysComeWithAHint) {
  const auto v = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "version": 2, "links": {}})"));
  ASSERT_FALSE(v.HasValue());
  EXPECT_NE(v.Error().SupportData().find("unknown key 'version' (MAVLink v1 is not supported"), std::string::npos)
      << v.Error().SupportData();
  const auto h = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "heartbeat_hz": 1, "links": {}})"));
  ASSERT_FALSE(h.HasValue());
  EXPECT_NE(h.Error().SupportData().find("unknown key 'heartbeat_hz' (the daemon sends no HEARTBEAT"),
            std::string::npos)
      << h.Error().SupportData();
}

TEST(DaemonConfig, LegacyHeartbeatKeyInsideALinkComesWithAHintAndTheLinkName) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({
    "system_id": 1, "component_id": 1,
    "links": {"radio_a": {"driver": "serial_radio", "heartbeat_hz": 1}}})"));
  ASSERT_FALSE(cfg.HasValue());
  const auto t = cfg.Error().SupportData();
  EXPECT_NE(t.find("link 'radio_a': unknown key 'heartbeat_hz' (the daemon sends no HEARTBEAT"), std::string::npos)
      << t;
}

TEST(DaemonConfig, WrongTypesAndBadModeAreReported) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({
    "system_id": "one", "component_id": 1, "socket_mode": "99", "links": {}})"));
  ASSERT_FALSE(cfg.HasValue());
  const auto t = cfg.Error().SupportData();
  EXPECT_NE(t.find("wrong type"), std::string::npos) << t;
  EXPECT_NE(t.find("socket_mode"), std::string::npos) << t;
}

TEST(DaemonConfig, MethodTimeoutDefaultsTo1000AndIsPerLink) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({
    "system_id": 1, "component_id": 1,
    "links": {"radio_a": {"driver": "serial_radio"},
              "radio_b": {"driver": "serial_radio", "method_timeout_ms": 4000}}})"));
  ASSERT_TRUE(cfg.HasValue()) << cfg.Error().SupportData();
  ASSERT_EQ(cfg.Value().links.size(), 2U);
  for (const auto& l : cfg.Value().links) {
    EXPECT_EQ(l.method_timeout_ms, l.name == "radio_b" ? 4000U : 1000U) << l.name;
  }
}

TEST(DaemonConfig, MethodTimeoutMustBeAPositiveInteger) {
  for (const char* bad : {"0", "-5", "1.5", R"("x")", "4294967296"}) {
    const auto cfg = ParseDaemonConfig(json::parse(
        std::string(R"({"system_id": 1, "component_id": 1,
          "links": {"radio_a": {"driver": "serial_radio", "method_timeout_ms": )") + bad + "}}}"));
    ASSERT_FALSE(cfg.HasValue()) << bad;
    EXPECT_NE(cfg.Error().SupportData().find("link 'radio_a': "), std::string::npos) << cfg.Error().SupportData();
    EXPECT_NE(cfg.Error().SupportData().find("method_timeout_ms"), std::string::npos) << cfg.Error().SupportData();
  }
}

TEST(DaemonConfig, RestartBackoffDefaultsAndPartialValues) {
  const auto def = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "links": {}})"));
  ASSERT_TRUE(def.HasValue()) << def.Error().SupportData();
  EXPECT_EQ(def.Value().restart_backoff.min.count(), 500);
  EXPECT_EQ(def.Value().restart_backoff.max.count(), 30000);
  const auto part = ParseDaemonConfig(
      json::parse(R"({"system_id": 1, "component_id": 1, "restart_backoff": {"max_ms": 4000}, "links": {}})"));
  ASSERT_TRUE(part.HasValue()) << part.Error().SupportData();
  EXPECT_EQ(part.Value().restart_backoff.min.count(), 500);
  EXPECT_EQ(part.Value().restart_backoff.max.count(), 4000);
}

TEST(DaemonConfig, RestartBackoffErrorsAndTheOldKey) {
  for (const char* bad : {R"({"min_ms": 0})", R"({"min_ms": 600, "max_ms": 500})", R"({"min_ms": "x"})", "5"}) {
    const auto cfg = ParseDaemonConfig(json::parse(
        std::string(R"({"system_id": 1, "component_id": 1, "links": {}, "restart_backoff": )") + bad + "}"));
    ASSERT_FALSE(cfg.HasValue()) << bad;
    EXPECT_NE(std::string(cfg.Error().SupportData()).find("restart_backoff"), std::string::npos) << bad;
  }
  const auto old = ParseDaemonConfig(
      json::parse(R"({"system_id": 1, "component_id": 1, "restart_delay_ms": 1500, "links": {}})"));
  ASSERT_FALSE(old.HasValue());
  const std::string t{old.Error().SupportData()};
  EXPECT_NE(t.find("restart_delay_ms"), std::string::npos) << t;
  EXPECT_NE(t.find("restart_backoff"), std::string::npos) << t;
}

TEST(DaemonConfig, LoadFromFileAndMissingOrBrokenFile) {
  const std::string path = "/tmp/srp_mavd_cfg_" + std::to_string(::getpid()) + ".json";
  std::ofstream(path) << R"({"system_id": 3, "component_id": 4, "links": {}})";
  const auto ok = LoadDaemonConfig(path);
  ASSERT_TRUE(ok.HasValue()) << ok.Error().SupportData();
  EXPECT_EQ(ok.Value().system_id, 3);
  std::ofstream(path) << "{ not json";
  const auto broken = LoadDaemonConfig(path);
  ASSERT_FALSE(broken.HasValue());
  EXPECT_NE(broken.Error().SupportData().find(path), std::string::npos);
  std::remove(path.c_str());
  const auto missing = LoadDaemonConfig(path);
  ASSERT_FALSE(missing.HasValue());
  EXPECT_NE(missing.Error().SupportData().find(path), std::string::npos);
}

TEST(DaemonConfig, DuplicateSigningLinkIdIsAConfigError) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "links": {
    "a": {"driver": "serial_radio", "signing": {"enabled": true, "key_file": "/k", "link_id": 4}},
    "b": {"driver": "serial_radio", "signing": {"enabled": true, "key_file": "/k", "link_id": 4}}}})"));
  ASSERT_FALSE(cfg.HasValue());
  const auto text = cfg.Error().SupportData();
  EXPECT_NE(text.find("link_id=4"), std::string::npos) << text;
  EXPECT_NE(text.find("'a'"), std::string::npos) << text;  // names the second link with that id
  EXPECT_NE(text.find("'b'"), std::string::npos) << text;
}

TEST(DaemonConfig, SameNonEmptyStateFileOnTwoSignedLinksIsAConfigError) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "links": {
    "a": {"driver": "serial_radio", "signing": {"enabled": true, "key_file": "/k", "link_id": 1, "state_file": "/s"}},
    "b": {"driver": "serial_radio", "signing": {"enabled": true, "key_file": "/k", "link_id": 2, "state_file": "/s"}}}})"));
  ASSERT_FALSE(cfg.HasValue());
  const auto text = cfg.Error().SupportData();
  EXPECT_NE(text.find("state_file '/s'"), std::string::npos) << text;
}

TEST(DaemonConfig, UnsignedLinksWithDefaultLinkIdAndEmptyStateFilesAreFine) {
  // two links without signing (default link_id 0) and two signed ones with different ids and an empty state_file
  const auto cfg = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "links": {
    "a": {"driver": "serial_radio"},
    "b": {"driver": "serial_radio", "signing": {"enabled": false, "link_id": 0}},
    "c": {"driver": "serial_radio", "signing": {"enabled": true, "key_file": "/k", "link_id": 1}},
    "d": {"driver": "serial_radio", "signing": {"enabled": true, "key_file": "/k", "link_id": 2}}}})"));
  ASSERT_TRUE(cfg.HasValue()) << cfg.Error().SupportData();
  EXPECT_EQ(cfg.Value().links.size(), 4U);
}

TEST(DaemonConfig, BandwidthFieldsAndDefaults) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "links": {
    "a": {"driver": "serial_radio", "air_rate_bps": 2400, "max_utilization": 0.5, "mtu": 58},
    "b": {"driver": "serial_radio"},
    "c": {"driver": "serial_radio", "air_rate_bps": 9600, "max_utilization": 1}}})"));
  ASSERT_TRUE(cfg.HasValue()) << cfg.Error().SupportData();
  const auto& l = cfg.Value().links;
  ASSERT_EQ(l.size(), 3U);
  EXPECT_EQ(l[0].air_rate_bps, 2400U);
  EXPECT_DOUBLE_EQ(l[0].max_utilization, 0.5);
  EXPECT_EQ(l[0].mtu, 58U);
  EXPECT_EQ(l[1].air_rate_bps, 0U);  // no budget
  EXPECT_DOUBLE_EQ(l[1].max_utilization, 0.8);
  EXPECT_EQ(l[1].mtu, 0U);
  EXPECT_DOUBLE_EQ(l[2].max_utilization, 1.0);
}

TEST(DaemonConfig, BadBandwidthFieldsAreReportedNamingTheLink) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "links": {
    "a": {"driver": "x", "air_rate_bps": 0},
    "b": {"driver": "x", "air_rate_bps": 9.5},
    "c": {"driver": "x", "max_utilization": 0},
    "d": {"driver": "x", "max_utilization": 1.5},
    "e": {"driver": "x", "mtu": 11},
    "f": {"driver": "x", "mtu": 281},
    "g": {"driver": "x", "mtu": "big"}}})"));
  ASSERT_FALSE(cfg.HasValue());
  const auto t = cfg.Error().SupportData();
  for (const char* name : {"link 'a'", "link 'b'", "link 'c'", "link 'd'", "link 'e'",
                           "link 'f'", "link 'g'"}) {
    EXPECT_NE(t.find(name), std::string::npos) << name << ": " << t;
  }
  EXPECT_NE(t.find("air_rate_bps"), std::string::npos) << t;
  EXPECT_NE(t.find("max_utilization"), std::string::npos) << t;
  EXPECT_NE(t.find("mtu"), std::string::npos) << t;
}

}  // namespace
}  // namespace mavlink_daemon
}  // namespace srp
