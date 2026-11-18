/**
 * Copyright (c) 2024
 *
 * @file mavlink_frame.h
 * @brief Ramki MAVLink v1/v2: CRC (X.25 + crc_extra), budowanie, parser
 * strumieniowy z resynchronizacją, podpis v2 (SHA-256 48 bit).
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

/// MCRF4XX (X.25) — CRC MAVLinka, start 0xFFFF.
std::uint16_t CrcAccumulate(std::uint16_t crc, std::uint8_t byte);
std::uint16_t CrcCalculate(std::span<const std::uint8_t> data);

struct SigningParams {
  std::span<const std::uint8_t> key;  // 32 B
  std::uint8_t link_id{0};
  std::uint64_t timestamp{0};  // 48 bit, jednostka 10 us od 2015-01-01
};

struct Frame {
  std::uint8_t version{2};
  std::uint8_t incompat_flags{0};
  std::uint8_t compat_flags{0};
  std::uint8_t seq{0};
  std::uint8_t sysid{0};
  std::uint8_t compid{0};
  std::uint32_t msgid{0};
  std::vector<std::uint8_t> payload;  // jak na przewodzie (v2: obcięte zera)
  std::uint16_t crc{0};
  bool has_signature{false};
  std::uint8_t link_id{0};
  std::uint64_t timestamp{0};
  std::array<std::uint8_t, 6> signature{};
  std::vector<std::uint8_t> raw;  // cała ramka (do weryfikacji podpisu)
  /// Wynik weryfikacji podpisu ustawiony przez filtr parsera (wartość SignatureVerdict).
  std::uint8_t sig_verdict{0};
};

/// Buduje ramkę. `payload` = pełny (bez obcięcia); v2 obcina końcowe zera
/// (zostaje >= 1 bajt). Podpis tylko dla v2 i gdy `signing` != nullptr.
std::vector<std::uint8_t> BuildFrame(std::uint8_t version, std::uint8_t seq,
                                     std::uint8_t sysid, std::uint8_t compid,
                                     std::uint32_t msgid,
                                     std::span<const std::uint8_t> payload,
                                     std::uint8_t crc_extra,
                                     const SigningParams* signing = nullptr);

/// Podpis 48 bit dla ramki (bez 13 B sygnatury): klucz + nagłówek + payload +
/// crc + link_id + timestamp.
std::array<std::uint8_t, 6> ComputeSignature(
    std::span<const std::uint8_t> key, std::span<const std::uint8_t> frame_wo_signature,
    std::uint8_t link_id, std::uint64_t timestamp);

/// Dopełnia obcięty payload v2 zerami do `full_len`.
std::vector<std::uint8_t> ExpandPayload(const std::vector<std::uint8_t>& payload,
                                        std::size_t full_len);

class FrameParser {
 public:
  /// crc_extra dla znanego msgid; nullopt = nieznany (ramka porzucona).
  using CrcExtraLookup = std::function<std::optional<std::uint8_t>(std::uint32_t)>;

  struct Stats {
    std::uint64_t frames{0};
    std::uint64_t crc_errors{0};
    std::uint64_t unknown_msgid{0};
    std::uint64_t bad_incompat_flags{0};
    std::uint64_t garbage_bytes{0};
    std::uint64_t filter_rejected{0};  ///< ramki odrzucone przez filtr (zły podpis)
  };

  /// Filtr wołany dla ramki z poprawnym CRC, zanim parser uzna ją za całą. false = ramka
  /// fałszywa (np. urwana podpisana ramka, która połknęła bajty następnej): parser wznawia
  /// skanowanie od bajtu PO znaczniku STX, tak jak przy złym CRC. Filtr może uzupełnić `Frame`.
  using FrameFilter = std::function<bool(Frame&)>;

  explicit FrameParser(CrcExtraLookup lookup) : lookup_{std::move(lookup)} {}

  /// Dokleja bajty i zwraca w `out` wszystkie kompletne, poprawne CRC ramki.
  void Feed(std::span<const std::uint8_t> data, std::vector<Frame>* out);
  const Stats& stats() const { return stats_; }
  void Reset() { buf_.clear(); }
  void SetFilter(FrameFilter f) { filter_ = std::move(f); }

 private:
  /// Długość kompletnej, poprawnej ramki zaczynającej się w buf_[pos] (0 = brak).
  std::size_t ValidFrameLengthAt(std::size_t pos) const;

  CrcExtraLookup lookup_;
  FrameFilter filter_;
  std::vector<std::uint8_t> buf_;
  Stats stats_;
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_MAVLINK_FRAME_H_
