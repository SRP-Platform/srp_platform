/**
 * Copyright (c) 2024
 *
 * @file mavlink_signing.h
 * @brief Podpis MAVLink 2 po stronie łącza: klucz z pliku, znacznik czasu
 * (R1: podłoga z pliku stanu, brak RTC na BBB), weryfikacja i ochrona przed
 * powtórzeniem.
 */
#ifndef ARA_COM_MAVLINK_MAVLINK_SIGNING_H_
#define ARA_COM_MAVLINK_MAVLINK_SIGNING_H_

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>  // NOLINT
#include <string>
#include <tuple>

#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

using SigningKey = std::array<std::uint8_t, 32>;

/// Wczytuje klucz: 64 znaki hex (białe znaki na końcach ignorowane).
/// Brak pliku / zła długość / nie-hex -> błąd z jasnym opisem.
ara::core::Result<SigningKey> LoadSigningKey(const std::string& path);

/// Jednostka znacznika czasu: 10 us od 2015-01-01; 0, gdy zegar < 2015.
std::uint64_t SystemTimestamp10us();

constexpr std::uint64_t kTimestampUnitsPerSecond = 100000;
/// Przed tą datą (2020-01-01) zegar uznajemy za nieustawiony (BBB bez RTC).
constexpr std::uint64_t kClockSanityThreshold = 157766400ULL * kTimestampUnitsPerSecond;
constexpr std::uint64_t kStartupFloor = 60 * kTimestampUnitsPerSecond;  // R1: +60 s

enum class SignatureVerdict {
  kOk,            ///< poprawny podpis (albo niepodpisana i accept_unsigned)
  kUnsigned,      ///< niepodpisana, a accept_unsigned=false
  kBadSignature,  ///< zły podpis
  kReplay         ///< znacznik czasu <= ostatni znany strumienia / zbyt stary nowy strumień
};

class LinkSigner {
 public:
  using NowFn = std::function<std::uint64_t()>;
  using WarnFn = std::function<void(const std::string&)>;

  struct Config {
    SigningKey key{};
    std::uint8_t link_id{0};
    bool accept_unsigned{false};
    std::string state_file;  // pusty = bez persist
  };

  LinkSigner(Config cfg, NowFn now, WarnFn warn = {});

  /// R1: czyta plik stanu RAZ; ustawia podłogę persisted + 60 s.
  void LoadPersisted();

  /// Znacznik czasu następnej ramki nadawczej: max(now, last_tx+1, last_peer+1)
  /// (po starcie z podłogą z pliku). Monotoniczny.
  std::uint64_t NextTimestamp();

  /// Co 10 s (wołane z pętli TX): zapisuje last_tx do pliku stanu.
  void PersistIfDue(std::uint64_t now_ms);
  /// Zapis stanu bez ograniczenia 10 s (zatrzymanie łącza); tylko gdy stan się zmienił.
  void PersistNow();

  SignatureVerdict Verify(const Frame& frame);

  const SigningKey& key() const { return cfg_.key; }
  std::uint8_t link_id() const { return cfg_.link_id; }
  bool accept_unsigned() const { return cfg_.accept_unsigned; }
  std::uint64_t last_tx() const;
  std::uint64_t last_peer() const;

 private:
  using StreamKey = std::tuple<std::uint8_t, std::uint8_t, std::uint8_t>;
  void Warn(const std::string& msg, bool* once_flag);
  void PersistImpl(std::uint64_t now_ms, bool force);

  Config cfg_;
  NowFn now_;
  WarnFn warn_;
  mutable std::mutex mutex_;
  std::uint64_t last_tx_{0};
  std::uint64_t last_peer_{0};   // największy poprawny znacznik od peera
  bool have_persisted_{false};
  std::map<StreamKey, std::uint64_t> streams_;
  std::uint64_t last_persist_ms_{0};
  std::uint64_t last_persisted_value_{0};
  std::uint64_t last_persisted_peer_{0};
  bool warned_clock_{false};
  bool warned_persist_{false};
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_MAVLINK_SIGNING_H_
