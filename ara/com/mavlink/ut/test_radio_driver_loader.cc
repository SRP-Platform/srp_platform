/**
 * @file test_radio_driver_loader.cc
 * @brief Driver registry and the `.so` plugin loader: duplicate id, wrong API version, missing
 * symbols, unknown driver, replacing the built-in `serial_radio`, one `.so` = two drivers.
 * @copyright Copyright (c) 2024
 */
#include <gtest/gtest.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ara/com/mavlink/radio_driver_loader.h"
#include "ara/com/mavlink/serial_radio.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {

std::string EnvPath(const char* name) {
  const char* v = std::getenv(name);
  if (v == nullptr) {
    return {};
  }
  char buf[PATH_MAX];
  return ::realpath(v, buf) != nullptr ? std::string{buf} : std::string{v};
}

bool Contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

class LoaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char tmpl[] = "/tmp/srp_mav_plugins_XXXXXX";
    ASSERT_NE(::mkdtemp(tmpl), nullptr);
    dir_ = tmpl;
    log_ = [this](LogLevel, const std::string& m) { logs_.push_back(m); };
  }
  void TearDown() override {
    for (const auto& f : created_) {
      ::unlink(f.c_str());
    }
    ::rmdir(dir_.c_str());
  }
  /// Adds a plugin to the directory as `as` (a symlink to the file in runfiles).
  void Put(const char* env, const std::string& as) {
    const std::string src = EnvPath(env);
    ASSERT_FALSE(src.empty()) << env;
    const std::string dst = dir_ + "/" + as;
    ASSERT_EQ(::symlink(src.c_str(), dst.c_str()), 0);
    created_.push_back(dst);
  }
  void PutText(const std::string& as, const std::string& body) {
    const std::string dst = dir_ + "/" + as;
    std::ofstream(dst) << body;
    created_.push_back(dst);
  }
  bool LogContains(const std::string& what) const {
    return std::any_of(logs_.begin(), logs_.end(), [&](const std::string& l) { return Contains(l, what); });
  }
  std::string dir_;
  std::vector<std::string> created_;
  std::vector<std::string> logs_;
  LogFn log_;
};

TEST_F(LoaderTest, OnePluginRegistersTwoDrivers) {
  Put("PLUGIN_OK", "a_plugin.so");
  RadioDriverRegistry reg{log_};
  ASSERT_TRUE(reg.AddBuiltin(kSerialRadioId, SerialRadioFactory()).HasValue());
  RadioDriverLoader loader{&reg, log_};
  const auto r = loader.LoadDirectory(dir_);
  ASSERT_TRUE(r.HasValue()) << r.Error().SupportData();
  const auto builtin = reg.Create("serial_radio", "/x.json");  // the built-in driver stays
  ASSERT_TRUE(builtin.HasValue());
  EXPECT_NE(builtin.Value()->Name(), "test_serial_a");
  const auto a = reg.Create("test_serial_a", "/x.json");
  const auto b = reg.Create("test_serial_b", "/x.json");
  ASSERT_TRUE(a.HasValue());
  ASSERT_TRUE(b.HasValue());
  EXPECT_EQ(a.Value()->Name(), "test_serial_a");
  EXPECT_EQ(b.Value()->Name(), "test_serial_b");
}

TEST_F(LoaderTest, LoadingTheSameDirectoryAgainIsHarmless) {
  Put("PLUGIN_OK", "a_plugin.so");
  RadioDriverRegistry reg{log_};
  RadioDriverLoader loader{&reg, log_};
  ASSERT_TRUE(loader.LoadDirectory(dir_).HasValue());
  // A daemon restart loads the directory again: already registered ids are not duplicates.
  EXPECT_TRUE(loader.LoadDirectory(dir_).HasValue());
  EXPECT_TRUE(reg.Create("test_serial_a", "/x.json").HasValue());
}

TEST_F(LoaderTest, DuplicateIdBetweenPluginsIsStartError) {
  Put("PLUGIN_OK", "a_plugin.so");
  Put("PLUGIN_DUP", "b_plugin.so");
  RadioDriverRegistry reg{log_};
  RadioDriverLoader loader{&reg, log_};
  const auto r = loader.LoadDirectory(dir_);
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPluginError));
  EXPECT_TRUE(Contains(r.Error().SupportData(), "duplicate driver id 'test_serial_a'")) << r.Error().SupportData();
  EXPECT_TRUE(Contains(r.Error().SupportData(), "b_plugin.so"));
  EXPECT_TRUE(LogContains("duplicate driver id"));  // a readable log
}

TEST_F(LoaderTest, WrongApiVersionIsStartError) {
  Put("PLUGIN_BADVER", "bad.so");
  RadioDriverRegistry reg{log_};
  RadioDriverLoader loader{&reg, log_};
  const auto r = loader.LoadDirectory(dir_);
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(Contains(r.Error().SupportData(), "API version")) << r.Error().SupportData();
  EXPECT_TRUE(Contains(r.Error().SupportData(), "bad.so"));
  EXPECT_FALSE(reg.Create("never_registered", "").HasValue());  // the plugin registered nothing
  EXPECT_TRUE(LogContains("API version"));
}

TEST_F(LoaderTest, LibraryWithoutPluginSymbolsIsStartError) {
  Put("PLUGIN_NOSYM", "plain.so");
  RadioDriverRegistry reg{log_};
  RadioDriverLoader loader{&reg, log_};
  const auto r = loader.LoadDirectory(dir_);
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(Contains(r.Error().SupportData(), "not a driver plugin")) << r.Error().SupportData();
}

TEST_F(LoaderTest, CorruptSoFileIsStartError) {
  PutText("junk.so", "this is not an ELF file");
  RadioDriverRegistry reg{log_};
  RadioDriverLoader loader{&reg, log_};
  const auto r = loader.LoadDirectory(dir_);
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(Contains(r.Error().SupportData(), "dlopen")) << r.Error().SupportData();
}

TEST_F(LoaderTest, GoodPluginsStayLoadedWhenAnotherFails) {
  Put("PLUGIN_BADVER", "a_bad.so");
  Put("PLUGIN_OK", "b_good.so");
  RadioDriverRegistry reg{log_};
  RadioDriverLoader loader{&reg, log_};
  EXPECT_FALSE(loader.LoadDirectory(dir_).HasValue());
  EXPECT_TRUE(reg.Create("test_serial_a", "/x.json").HasValue());
}

TEST_F(LoaderTest, PluginWithBuiltinIdReplacesBuiltinAndLogsIt) {
  Put("PLUGIN_REPLACE", "serial.so");
  RadioDriverRegistry reg{log_};
  ASSERT_TRUE(reg.AddBuiltin(kSerialRadioId, SerialRadioFactory()).HasValue());
  const auto builtin = reg.Create("serial_radio", "/x.json");
  ASSERT_TRUE(builtin.HasValue());
  EXPECT_NE(builtin.Value()->Name(), "replacement_serial_radio");
  RadioDriverLoader loader{&reg, log_};
  const auto r = loader.LoadDirectory(dir_);
  ASSERT_TRUE(r.HasValue()) << r.Error().SupportData();
  const auto replaced = reg.Create("serial_radio", "/x.json");
  ASSERT_TRUE(replaced.HasValue());
  EXPECT_EQ(replaced.Value()->Name(), "replacement_serial_radio");
  EXPECT_TRUE(LogContains("replaces the built-in driver 'serial_radio'"));
  EXPECT_TRUE(reg.TakeErrors().empty());
}

TEST_F(LoaderTest, EmptyDirectoryAndNonSoFilesAreIgnored) {
  PutText("readme.txt", "x");
  PutText("lib.so.bak", "x");
  RadioDriverRegistry reg{log_};
  RadioDriverLoader loader{&reg, log_};
  EXPECT_TRUE(loader.LoadDirectory(dir_).HasValue());
  EXPECT_EQ(reg.size(), 0U);
}

TEST_F(LoaderTest, MissingDirectoryIsError) {
  RadioDriverRegistry reg{log_};
  RadioDriverLoader loader{&reg, log_};
  const auto r = loader.LoadDirectory("/tmp/srp_mav_no_such_dir_xyz");
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(Contains(r.Error().SupportData(), "/tmp/srp_mav_no_such_dir_xyz"));
}

TEST_F(LoaderTest, UnknownDriverListsKnownIds) {
  RadioDriverRegistry reg{log_};
  ASSERT_TRUE(reg.AddBuiltin(kSerialRadioId, SerialRadioFactory()).HasValue());
  const auto r = reg.Create("unknown_radio", "/x.json");
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPluginError));
  EXPECT_TRUE(Contains(r.Error().SupportData(), "unknown_radio"));
  EXPECT_TRUE(Contains(r.Error().SupportData(), "serial_radio"));
}

TEST_F(LoaderTest, FactoryReturningNullIsReportedWithConfigPath) {
  RadioDriverRegistry reg{log_};
  ASSERT_TRUE(reg.AddBuiltin("nullish", [](const std::string&) { return std::shared_ptr<IRadioDriver>{}; }).HasValue());
  const auto r = reg.Create("nullish", "/etc/srp/x.json");
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(Contains(r.Error().SupportData(), "/etc/srp/x.json"));
}

TEST(RadioDriverRegistry, RejectsEmptyIdNullFactoryAndDuplicateBuiltin) {
  RadioDriverRegistry reg;
  EXPECT_FALSE(reg.AddBuiltin("", SerialRadioFactory()).HasValue());
  EXPECT_FALSE(reg.AddBuiltin("x", RadioDriverFactory{}).HasValue());
  ASSERT_TRUE(reg.AddBuiltin("serial_radio", SerialRadioFactory()).HasValue());
  EXPECT_FALSE(reg.AddBuiltin("serial_radio", SerialRadioFactory()).HasValue());
  reg.BeginSource("/lib/a.so");
  ASSERT_TRUE(reg.Register("p", SerialRadioFactory()).HasValue());
  reg.BeginSource("/lib/b.so");
  EXPECT_FALSE(reg.Register("p", SerialRadioFactory()).HasValue());  // plugin vs plugin = error
  EXPECT_EQ(reg.TakeErrors().size(), 4U);
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
