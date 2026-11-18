/**
 * Copyright (c) 2024
 *
 * @file mavlink_signing.cc
 */
#include "ara/com/mavlink/mavlink_signing.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <utility>

#include "ara/com/mavlink/mavlink_build_time.h"
#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
constexpr std::uint64_t kEpoch2015 = 1420070400ULL;  // 2015-01-01 UTC (seconds since 1970)

int HexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}  // namespace

ara::core::Result<SigningKey> LoadSigningKey(const std::string& path) {
  std::ifstream in{path};
  if (!in) {
    return MakeTransportError(TransportErrc::kPersistent,
                              "signing: cannot open key file '" + path + "'");
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string text = ss.str();
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' ||
                           text.back() == ' ' || text.back() == '\t')) {
    text.pop_back();
  }
  if (text.size() != 64) {
    return MakeTransportError(
        TransportErrc::kPersistent,
        "signing: key in '" + path + "' has " + std::to_string(text.size()) +
            " characters, 64 hex characters (32 B) required");
  }
  SigningKey key{};
  for (std::size_t i = 0; i < 32; ++i) {
    const int hi = HexVal(text[2 * i]);
    const int lo = HexVal(text[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      return MakeTransportError(TransportErrc::kPersistent,
                                "signing: key in '" + path + "' contains a non-hex character");
    }
    key[i] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  return key;
}

std::uint64_t SystemTimestamp10us() {
  timespec ts{};
  ::clock_gettime(CLOCK_REALTIME, &ts);
  if (static_cast<std::uint64_t>(ts.tv_sec) < kEpoch2015) {
    return 0;
  }
  return (static_cast<std::uint64_t>(ts.tv_sec) - kEpoch2015) * kTimestampUnitsPerSecond +
         static_cast<std::uint64_t>(ts.tv_nsec) / 10000ULL;
}

std::uint64_t BuildTimestamp10us() {
  return kBuildUnixTime > kEpoch2015 ? (kBuildUnixTime - kEpoch2015) * kTimestampUnitsPerSecond : 0;
}

LinkSigner::LinkSigner(Config cfg, NowFn now, WarnFn warn)
    : cfg_{std::move(cfg)}, now_{std::move(now)}, warn_{std::move(warn)} {}

void LinkSigner::Warn(const std::string& msg, bool* once_flag) {
  if (*once_flag) {
    return;
  }
  *once_flag = true;
  if (warn_) {
    warn_(msg);
  }
}

void LinkSigner::LoadPersisted() {
  if (cfg_.state_file.empty()) {
    return;
  }
  std::ifstream in{cfg_.state_file};
  std::lock_guard<std::mutex> lock{mutex_};
  if (!in) {
    Warn("signing: state file '" + cfg_.state_file + "' is missing (running without persistence)",
         &warned_persist_);
    return;
  }
  std::uint64_t v = 0;
  if (in >> v) {
    have_persisted_ = true;
    last_tx_ = std::max(last_tx_, v + kStartupFloor);  // one-time floor
    std::uint64_t peer = 0;
    if (in >> peer) {
      last_peer_ = std::max(last_peer_, peer);  // the last valid peer timestamp survives restarts too
    }
    last_persisted_value_ = v;
    last_persisted_peer_ = last_peer_;
  }
}

std::uint64_t LinkSigner::NextTimestamp() {
  const std::uint64_t now = now_();
  std::lock_guard<std::mutex> lock{mutex_};
  if (now < kClockSanityThreshold && !have_persisted_ && last_peer_ == 0) {
    Warn("signing: system clock is not set and there is no saved state or peer data — "
         "timestamps start from the build date (the peer may reject frames)", &warned_clock_);
  }
  std::uint64_t ts = std::max({now, cfg_.min_timestamp, last_tx_ + 1, last_peer_ + 1});
  last_tx_ = ts;
  return ts;
}

void LinkSigner::PersistNow() { PersistImpl(0, true); }

void LinkSigner::PersistIfDue(std::uint64_t now_ms) { PersistImpl(now_ms, false); }

void LinkSigner::PersistImpl(std::uint64_t now_ms, bool force) {
  if (cfg_.state_file.empty()) {
    return;
  }
  std::uint64_t value = 0;
  std::uint64_t peer_value = 0;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!force && last_persist_ms_ != 0 && now_ms - last_persist_ms_ < 10000) {
      return;
    }
    if (last_persisted_value_ == last_tx_ && last_persisted_peer_ == last_peer_ &&
        last_persist_ms_ != 0) {
      last_persist_ms_ = now_ms == 0 ? 1 : now_ms;
      return;  // unchanged: do not wear the eMMC
    }
    last_persist_ms_ = now_ms == 0 ? 1 : now_ms;
    value = last_tx_;
    peer_value = last_peer_;
    last_persisted_value_ = value;
    last_persisted_peer_ = peer_value;
  }
  const std::string tmp = cfg_.state_file + ".tmp";
  {
    std::ofstream out{tmp, std::ios::trunc};
    if (!out) {
      std::lock_guard<std::mutex> lock{mutex_};
      Warn("signing: cannot write state '" + cfg_.state_file +
           "' (missing directory or permissions) — running without persistence", &warned_persist_);
      return;
    }
    out << value << "\n" << peer_value << "\n";
  }
  if (std::rename(tmp.c_str(), cfg_.state_file.c_str()) != 0) {
    std::lock_guard<std::mutex> lock{mutex_};
    Warn("signing: renaming the state file failed", &warned_persist_);
  }
}

SignatureVerdict LinkSigner::VerifySignature(const Frame& f,
                                             std::span<const std::uint8_t> raw) const {
  if (!f.has_signature) {
    return cfg_.accept_unsigned ? SignatureVerdict::kOk : SignatureVerdict::kUnsigned;
  }
  if (raw.size() < kSignatureLen) {
    return SignatureVerdict::kBadSignature;
  }
  const auto expected =
      ComputeSignature(cfg_.key, raw.first(raw.size() - kSignatureLen), f.link_id, f.timestamp);
  std::uint8_t diff = 0;
  for (std::size_t i = 0; i < 6; ++i) {
    diff |= static_cast<std::uint8_t>(expected[i] ^ f.signature[i]);
  }
  return diff == 0 ? SignatureVerdict::kOk : SignatureVerdict::kBadSignature;
}

SignatureVerdict LinkSigner::CheckReplay(const Frame& f) const {
  if (!f.has_signature) {
    return SignatureVerdict::kOk;
  }
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = streams_.find(StreamKey{f.sysid, f.compid, f.link_id});
  if (it != streams_.end()) {
    return f.timestamp <= it->second ? SignatureVerdict::kReplay : SignatureVerdict::kOk;
  }
  // A new stream must not be older by more than 60 s than the own / last seen timestamp
  // (last_tx_ has the floor from the state file, so frames recorded before a restart fail).
  return f.timestamp + kStartupFloor < std::max(last_peer_, last_tx_) ? SignatureVerdict::kReplay
                                                                      : SignatureVerdict::kOk;
}

void LinkSigner::Commit(const Frame& f) {
  if (!f.has_signature) {
    return;
  }
  std::lock_guard<std::mutex> lock{mutex_};
  streams_[StreamKey{f.sysid, f.compid, f.link_id}] = f.timestamp;
  last_peer_ = std::max(last_peer_, f.timestamp);
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
