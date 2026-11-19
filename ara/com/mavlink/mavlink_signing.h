/**
 * Copyright (c) 2024
 *
 * @file mavlink_signing.h
 * @brief MAVLink 2 signing on the link side: key from a file, timestamp (floor from a state
 * file and the build date, since the BBB has no RTC), signature verification and replay
 * protection.
 *
 * Receiving a frame is split in three steps so that the replay window only moves for frames
 * that are actually accepted: VerifySignature() (stateless, usable inside the parser filter),
 * CheckReplay() (read-only) and Commit() (called once the frame passed every other RX filter).
 */
#ifndef ARA_COM_MAVLINK_MAVLINK_SIGNING_H_
#define ARA_COM_MAVLINK_MAVLINK_SIGNING_H_

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>  // NOLINT
#include <span>  // NOLINT
#include <string>
#include <tuple>

#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

using SigningKey = std::array<std::uint8_t, 32>;

/// Loads the key: 64 hex characters (surrounding whitespace ignored).
/// Missing file / wrong length / non-hex -> error with a clear description.
ara::core::Result<SigningKey> LoadSigningKey(const std::string& path);

/// Timestamp unit: 10 us since 2015-01-01; 0 when the clock is before 2015.
std::uint64_t SystemTimestamp10us();

/// Build date of this binary as a timestamp (10 us since 2015-01-01); lower bound for signing
/// timestamps on a system whose clock is not set.
std::uint64_t BuildTimestamp10us();

constexpr std::uint64_t kTimestampUnitsPerSecond = 100000;
/// Before this date (2020-01-01) the system clock is considered unset (BBB without RTC).
constexpr std::uint64_t kClockSanityThreshold = 157766400ULL * kTimestampUnitsPerSecond;
/// Added to the persisted timestamp on start (the last value written may be up to 10 s old).
constexpr std::uint64_t kStartupFloor = 60 * kTimestampUnitsPerSecond;

enum class SignatureVerdict {
  kOk,            ///< valid signature (or unsigned and accept_unsigned)
  kUnsigned,      ///< unsigned while accept_unsigned=false
  kBadSignature,  ///< wrong signature
  kReplay         ///< timestamp <= last known of the stream / new stream too old
};

class LinkSigner {
 public:
  using NowFn = std::function<std::uint64_t()>;
  using WarnFn = std::function<void(const std::string&)>;

  struct Config {
    SigningKey key{};
    std::uint8_t link_id{0};
    bool accept_unsigned{false};
    std::string state_file;  // empty = no persistence
    /// Lower bound of every transmitted timestamp (the build date in production).
    std::uint64_t min_timestamp{0};
  };

  LinkSigner(Config cfg, NowFn now, WarnFn warn = {});

  /// Reads the state file ONCE; sets the floor persisted + 60 s.
  void LoadPersisted();

  /// Timestamp of the next transmitted frame:
  /// max(now, min_timestamp, persisted + 60 s, last_tx + 1, last_peer + 1). Strictly monotonic.
  std::uint64_t NextTimestamp();

  /// Every 10 s (called from the TX loop): writes last_tx to the state file.
  void PersistIfDue(std::uint64_t now_ms);
  /// Writes the state without the 10 s limit (link stop); only when the state changed.
  void PersistNow();

  /// Checks the signature of `frame` (`raw` = the whole frame as received). Does not touch the
  /// replay state. Unsigned frames give kOk when accept_unsigned, else kUnsigned.
  SignatureVerdict VerifySignature(const Frame& frame, std::span<const std::uint8_t> raw) const;
  /// Replay check of a frame whose signature is valid; read-only. kOk for unsigned frames.
  SignatureVerdict CheckReplay(const Frame& frame) const;
  /// Records the timestamp of an accepted frame (stream window and last peer timestamp).
  void Commit(const Frame& frame);

  const SigningKey& key() const { return cfg_.key; }
  std::uint8_t link_id() const { return cfg_.link_id; }

 private:
  using StreamKey = std::tuple<std::uint8_t, std::uint8_t, std::uint8_t>;
  void Warn(const std::string& msg, bool* once_flag);
  void PersistImpl(std::uint64_t now_ms, bool force);

  Config cfg_;
  NowFn now_;
  WarnFn warn_;
  mutable std::mutex mutex_;
  std::uint64_t last_tx_{0};
  std::uint64_t last_peer_{0};   // largest accepted timestamp from the peer
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
