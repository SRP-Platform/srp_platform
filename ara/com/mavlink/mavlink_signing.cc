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

#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
constexpr std::uint64_t kEpoch2015 = 1420070400ULL;  // 2015-01-01 UTC (s od 1970)

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
                              "signing: nie można otworzyć pliku klucza '" + path + "'");
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
        "signing: klucz w '" + path + "' ma " + std::to_string(text.size()) +
            " znaków, wymagane 64 znaki hex (32 B)");
  }
  SigningKey key{};
  for (std::size_t i = 0; i < 32; ++i) {
    const int hi = HexVal(text[2 * i]);
    const int lo = HexVal(text[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      return MakeTransportError(TransportErrc::kPersistent,
                                "signing: klucz w '" + path + "' zawiera znak spoza hex");
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
    Warn("signing: brak pliku stanu '" + cfg_.state_file + "' (działa bez persist)",
         &warned_persist_);
    return;
  }
  std::uint64_t v = 0;
  if (in >> v) {
    have_persisted_ = true;
    last_tx_ = std::max(last_tx_, v + kStartupFloor);  // jednorazowa podłoga (R1)
    std::uint64_t peer = 0;
    if (in >> peer) {
      last_peer_ = std::max(last_peer_, peer);  // rev. 6: last_valid_peer_ts też przeżywa restart
    }
    last_persisted_value_ = v;
    last_persisted_peer_ = last_peer_;
  }
}

std::uint64_t LinkSigner::NextTimestamp() {
  const std::uint64_t now = now_();
  std::lock_guard<std::mutex> lock{mutex_};
  if (now < kClockSanityThreshold && !have_persisted_ && last_peer_ == 0) {
    Warn("signing: zegar systemowy nieustawiony, brak stanu i danych od peera — "
         "znacznik czasu startuje od zera (peer może odrzucić ramki)", &warned_clock_);
  }
  std::uint64_t ts = std::max(now, last_tx_ + 1);
  ts = std::max(ts, last_peer_ + 1);
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
      return;  // bez zmiany: nie zużywamy eMMC
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
      Warn("signing: nie można zapisać stanu '" + cfg_.state_file +
           "' (brak katalogu/uprawnień) — działa bez persist", &warned_persist_);
      return;
    }
    out << value << "\n" << peer_value << "\n";
  }
  if (std::rename(tmp.c_str(), cfg_.state_file.c_str()) != 0) {
    std::lock_guard<std::mutex> lock{mutex_};
    Warn("signing: rename stanu nie powiódł się", &warned_persist_);
  }
}

SignatureVerdict LinkSigner::Verify(const Frame& f) {
  if (!f.has_signature) {
    return cfg_.accept_unsigned ? SignatureVerdict::kOk : SignatureVerdict::kUnsigned;
  }
  if (f.raw.size() < kSignatureLen) {
    return SignatureVerdict::kBadSignature;
  }
  const auto expected = ComputeSignature(
      cfg_.key, std::span<const std::uint8_t>{f.raw.data(), f.raw.size() - kSignatureLen},
      f.link_id, f.timestamp);
  std::uint8_t diff = 0;
  for (std::size_t i = 0; i < 6; ++i) {
    diff |= static_cast<std::uint8_t>(expected[i] ^ f.signature[i]);
  }
  if (diff != 0) {
    return SignatureVerdict::kBadSignature;
  }
  std::lock_guard<std::mutex> lock{mutex_};
  const StreamKey key{f.sysid, f.compid, f.link_id};
  const auto it = streams_.find(key);
  if (it != streams_.end()) {
    if (f.timestamp <= it->second) {
      return SignatureVerdict::kReplay;
    }
  } else if (f.timestamp + kStartupFloor < std::max(last_peer_, last_tx_)) {
    // Nowy strumień nie może być starszy o > 60 s niż własny/ostatnio widziany znacznik
    // (last_tx_ ma podłogę z pliku stanu, więc ramki nagrane przed restartem odpadają).
    return SignatureVerdict::kReplay;
  }
  streams_[key] = f.timestamp;
  last_peer_ = std::max(last_peer_, f.timestamp);
  return SignatureVerdict::kOk;
}

std::uint64_t LinkSigner::last_tx() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return last_tx_;
}

std::uint64_t LinkSigner::last_peer() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return last_peer_;
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
