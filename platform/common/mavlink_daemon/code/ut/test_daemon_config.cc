/**
 * Copyright (c) 2024
 *
 * @file test_daemon_config.cc
 * @brief Konfiguracja MavlinkDaemona: pola, wartości domyślne i czytelne błędy.
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
using nlohmann::json;

TEST(DaemonConfig, ParsesAFullConfiguration) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({
    "system_id": 7, "component_id": 2,
    "socket_path": "/tmp/x.sock", "socket_mode": "0600",
    "driver_dir": "/usr/lib/srp/mavlink_drivers", "restart_delay_ms": 1500,
    "links": {
      "lora": {"driver": "ebyte_e220", "driver_config": "/etc/srp/e220.json", "version": 1, "heartbeat_hz": 2.5,
               "signing": {"enabled": true, "key_file": "/k", "link_id": 9, "state_file": "/s", "accept_unsigned": true}},
      "radio_b": {"driver": "serial_radio", "driver_config": "/etc/srp/radio_b.json"}
    }})"));
  ASSERT_TRUE(cfg.HasValue()) << cfg.Error().SupportData();
  const auto& c = cfg.Value();
  EXPECT_EQ(c.system_id, 7);
  EXPECT_EQ(c.component_id, 2);
  EXPECT_EQ(c.socket_path, "/tmp/x.sock");
  EXPECT_EQ(c.socket_mode, 0600U);
  EXPECT_EQ(c.driver_dir, "/usr/lib/srp/mavlink_drivers");
  EXPECT_EQ(c.restart_delay.count(), 1500);
  ASSERT_EQ(c.links.size(), 2U);
  const auto& lora = c.links[0].name == "lora" ? c.links[0] : c.links[1];
  const auto& radio_b = c.links[0].name == "lora" ? c.links[1] : c.links[0];
  EXPECT_EQ(lora.driver, "ebyte_e220");
  EXPECT_EQ(lora.driver_config, "/etc/srp/e220.json");
  EXPECT_EQ(lora.version, 1);
  EXPECT_DOUBLE_EQ(lora.heartbeat_hz, 2.5);
  EXPECT_TRUE(lora.signing_enabled);
  EXPECT_EQ(lora.key_file, "/k");
  EXPECT_EQ(lora.signing_link_id, 9);
  EXPECT_EQ(lora.state_file, "/s");
  EXPECT_TRUE(lora.accept_unsigned);
  EXPECT_EQ(radio_b.driver, "serial_radio");
  EXPECT_EQ(radio_b.version, 2);              // domyślnie
  EXPECT_DOUBLE_EQ(radio_b.heartbeat_hz, 1.0);  // domyślnie
  EXPECT_FALSE(radio_b.signing_enabled);
}

TEST(DaemonConfig, DefaultsForOptionalFields) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "links": {}})"));
  ASSERT_TRUE(cfg.HasValue());
  EXPECT_EQ(cfg.Value().socket_path, "/run/srp_mavlink_daemon.sock");
  EXPECT_EQ(cfg.Value().socket_mode, 0660U);
  EXPECT_TRUE(cfg.Value().driver_dir.empty());
  EXPECT_EQ(cfg.Value().restart_delay.count(), 30000);
  EXPECT_TRUE(cfg.Value().links.empty());
}

TEST(DaemonConfig, ReportsAllProblemsAtOnceNamingTheLinks) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({
    "system_id": 0, "component_id": 300,
    "links": {
      "a": {"version": 3},
      "b": {"driver": ""},
      "c": {"driver": "x", "heartbeat_hz": -1, "signing": {"enabled": true}},
      "d": 5
    }})"));
  ASSERT_FALSE(cfg.HasValue());
  const auto t = cfg.Error().SupportData();
  EXPECT_NE(t.find("system_id"), std::string::npos) << t;
  EXPECT_NE(t.find("component_id"), std::string::npos) << t;
  EXPECT_NE(t.find("łącze 'a'"), std::string::npos) << t;
  EXPECT_NE(t.find("version"), std::string::npos) << t;
  EXPECT_NE(t.find("łącze 'b'"), std::string::npos) << t;
  EXPECT_NE(t.find("łącze 'c'"), std::string::npos) << t;
  EXPECT_NE(t.find("heartbeat_hz"), std::string::npos) << t;
  EXPECT_NE(t.find("key_file"), std::string::npos) << t;
  EXPECT_NE(t.find("łącze 'd'"), std::string::npos) << t;
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

TEST(DaemonConfig, WrongTypesAndBadModeAreReported) {
  const auto cfg = ParseDaemonConfig(json::parse(R"({
    "system_id": "jeden", "component_id": 1, "socket_mode": "99", "restart_delay_ms": -5, "links": {}})"));
  ASSERT_FALSE(cfg.HasValue());
  const auto t = cfg.Error().SupportData();
  EXPECT_NE(t.find("zły typ"), std::string::npos) << t;
  EXPECT_NE(t.find("socket_mode"), std::string::npos) << t;
  EXPECT_NE(t.find("restart_delay_ms"), std::string::npos) << t;
}

TEST(DaemonConfig, LoadFromFileAndMissingOrBrokenFile) {
  const std::string path = "/tmp/srp_mavd_cfg_" + std::to_string(::getpid()) + ".json";
  std::ofstream(path) << R"({"system_id": 3, "component_id": 4, "links": {}})";
  const auto ok = LoadDaemonConfig(path);
  ASSERT_TRUE(ok.HasValue()) << ok.Error().SupportData();
  EXPECT_EQ(ok.Value().system_id, 3);
  std::ofstream(path) << "{ nie json";
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
  EXPECT_NE(text.find("'a'"), std::string::npos) << text;  // wskazuje drugie łącze o tym id
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
  // dwa łącza bez podpisu (domyślny link_id 0) oraz dwa podpisane z różnymi id i pustym state_file
  const auto cfg = ParseDaemonConfig(json::parse(R"({"system_id": 1, "component_id": 1, "links": {
    "a": {"driver": "serial_radio"},
    "b": {"driver": "serial_radio", "signing": {"enabled": false, "link_id": 0}},
    "c": {"driver": "serial_radio", "signing": {"enabled": true, "key_file": "/k", "link_id": 1}},
    "d": {"driver": "serial_radio", "signing": {"enabled": true, "key_file": "/k", "link_id": 2}}}})"));
  ASSERT_TRUE(cfg.HasValue()) << cfg.Error().SupportData();
  EXPECT_EQ(cfg.Value().links.size(), 4U);
}

}  // namespace
}  // namespace mavlink_daemon
}  // namespace srp
