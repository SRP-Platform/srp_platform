/**
 * Copyright (c) 2024
 *
 * @file mavlink_frame.h
 * @brief MAVLink v1/v2 frames: CRC (X.25 + crc_extra), frame building, a streaming
 * parser with resynchronisation, and v2 signatures (48-bit truncated SHA-256).
 */
#ifndef ARA_COM_MAVLINK_MAVLINK_FRAME_H_
#define ARA_COM_MAVLINK_MAVLINK_FRAME_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>  // NOLINT
#include <utility>
#include <vector>

namespace ara {
namespace com {
namespace mavlink {

constexpr std::uint8_t kMagicV1 = 0xFE;
constexpr std::uint8_t kMagicV2 = 0xFD;
constexpr std::uint8_t kIncompatSigned = 0x01;
constexpr std::size_t kSignatureLen = 13;  // link_id(1) + timestamp(6) + sig(6)
constexpr std::size_t kMaxPayload = 255;
constexpr std::uint32_t kHeartbeatMsgId = 0;
constexpr std::uint8_t kHeartbeatCrcExtra = 50;
constexpr std::size_t kHeartbeatPayloadLen = 9;

/// Little-endian integer access shared by the frame codec, the IPC protocol and the daemon.
inline void PutLe(std::uint8_t* p, std::uint64_t v, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    p[i] = static_cast<std::uint8_t>(v >> (8 * i));
  }
}
inline std::uint64_t GetLe(const std::uint8_t* p, std::size_t n) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < n; ++i) {
    v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  }
  return v;
}

/// MCRF4XX (X.25) — the MAVLink CRC, initial value 0xFFFF.
std::uint16_t CrcAccumulate(std::uint16_t crc, std::uint8_t byte);
std::uint16_t CrcCalculate(std::span<const std::uint8_t> data);

struct SigningParams {
  std::span<const std::uint8_t> key;  // 32 B
  std::uint8_t link_id{0};
  std::uint64_t timestamp{0};  // 48 bit, unit 10 us since 2015-01-01
};

struct Frame {
  std::uint8_t version{2};
  std::uint8_t incompat_flags{0};
  std::uint8_t seq{0};
  std::uint8_t sysid{0};
  std::uint8_t compid{0};
  std::uint32_t msgid{0};
  std::vector<std::uint8_t> payload;  // as on the wire (v2: trailing zeros truncated)
  bool has_signature{false};
  std::uint8_t link_id{0};
  std::uint64_t timestamp{0};
  std::array<std::uint8_t, 6> signature{};
  /// Signature verdict set by the parser filter (a SignatureVerdict value).
  std::uint8_t sig_verdict{0};
};

/// Builds a frame. `payload` is the full payload; v2 truncates trailing zeros
/// (at least 1 byte stays). Signed only for v2 and when `signing` != nullptr.
std::vector<std::uint8_t> BuildFrame(std::uint8_t version, std::uint8_t seq,
                                     std::uint8_t sysid, std::uint8_t compid,
                                     std::uint32_t msgid,
                                     std::span<const std::uint8_t> payload,
                                     std::uint8_t crc_extra,
                                     const SigningParams* signing = nullptr);

/// 48-bit signature of a frame (without the 13 B signature block): key + header +
/// payload + crc + link_id + timestamp.
std::array<std::uint8_t, 6> ComputeSignature(
    std::span<const std::uint8_t> key, std::span<const std::uint8_t> frame_wo_signature,
    std::uint8_t link_id, std::uint64_t timestamp);

/// Pads a truncated v2 payload with zeros up to `full_len`.
std::vector<std::uint8_t> ExpandPayload(const std::vector<std::uint8_t>& payload,
                                        std::size_t full_len);

class FrameParser {
 public:
  /// crc_extra of a known msgid; nullopt = unknown (frame dropped).
  using CrcExtraLookup = std::function<std::optional<std::uint8_t>(std::uint32_t)>;

  struct Stats {
    std::uint64_t frames{0};
    std::uint64_t crc_errors{0};
    std::uint64_t unknown_msgid{0};
    std::uint64_t bad_incompat_flags{0};
    std::uint64_t garbage_bytes{0};
    std::uint64_t filter_rejected{0};  ///< frames rejected by the filter (bad signature)
  };

  /// Called for a frame with a valid CRC before the parser accepts it; `raw` is the whole frame
  /// as received (including the signature block), a view into the parser buffer. false = the
  /// frame is bogus (e.g. a truncated signed frame that swallowed the next frame's bytes): the
  /// parser resumes scanning right after the STX byte, as for a bad CRC. The filter may fill in
  /// `Frame`.
  using FrameFilter = std::function<bool(Frame&, std::span<const std::uint8_t> raw)>;

  explicit FrameParser(CrcExtraLookup lookup) : lookup_{std::move(lookup)} {}

  /// Appends bytes and returns all complete frames with a valid CRC in `out`.
  void Feed(std::span<const std::uint8_t> data, std::vector<Frame>* out);
  const Stats& stats() const { return stats_; }
  void Reset() { buf_.clear(); }
  void SetFilter(FrameFilter f) { filter_ = std::move(f); }

 private:
  enum class Probe {
    kNotStart,       ///< not a start-of-frame byte
    kNeedHeader,     ///< header not complete yet
    kBadFlags,       ///< unknown incompat flag
    kIncomplete,     ///< header ok, frame not complete yet
    kUnknownMsgId,   ///< CRC cannot be verified
    kBadCrc,
    kValid
  };
  struct Probed {
    Probe verdict{Probe::kNotStart};
    std::size_t hdr{0};
    std::size_t len{0};
    std::size_t total{0};
    std::uint32_t msgid{0};
    bool is_v2{false};
    bool is_signed{false};
  };
  /// Classifies the bytes starting at buf_[pos] without touching the statistics.
  Probed ProbeAt(std::size_t pos) const;

  CrcExtraLookup lookup_;
  FrameFilter filter_;
  std::vector<std::uint8_t> buf_;
  Stats stats_;
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_MAVLINK_FRAME_H_
