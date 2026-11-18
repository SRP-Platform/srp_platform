/**
 * Copyright (c) 2024
 *
 * @file mavlink_codec.h
 * @brief Kodek wiadomości: bajty SOME/IP (jak produkują konwertery platformy)
 * <-> payload MAVLink wg opisu pól z modelu (ModelMavlinkMessage).
 *
 * Układ na przewodzie MAVLink: pola posortowane malejąco wg rozmiaru elementu
 * (stabilnie, jak mavgen), liczby little-endian. Reprezentacja SOME/IP: pola
 * payloadu sklejone w kolejności z modelu, bez wypełnień; endian wg pola
 * (domyślnie little — tak działają konwertery platformy). `char[N]` w struct =
 * N bajtów dopełnionych zerami; pojedynczy string na najwyższym poziomie =
 * surowe bajty tekstu (do pierwszego NUL).
 */
#ifndef ARA_COM_MAVLINK_MAVLINK_CODEC_H_
#define ARA_COM_MAVLINK_MAVLINK_CODEC_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ara/core/model/mavlink_model_item.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {

/// Pola nagłówka wiadomości (nie są częścią payloadu SOME/IP).
struct MessageHeader {
  std::uint16_t seq{0};
  std::uint16_t instance{0};
  std::uint8_t target_system{0};
  std::uint8_t target_component{0};
  std::uint8_t status{0};
};

struct DecodedMessage {
  MessageHeader header;
  std::vector<std::uint8_t> someip;  ///< payload w reprezentacji SOME/IP
};

class MessageCodec {
 public:
  explicit MessageCodec(const ara::core::model::ModelMavlinkMessage& msg);

  std::uint32_t msg_id() const { return msg_id_; }
  std::uint8_t crc_extra() const { return crc_extra_; }
  const std::string& name() const { return name_; }
  /// Pełna długość payloadu MAVLink (bez obcinania zer v2).
  std::size_t full_payload_len() const { return full_len_; }
  /// Offset pola nagłówka (seq/instance/target/status) w pełnym payloadzie MAVLink; nullopt =
  /// wiadomość nie ma takiego pola.
  std::optional<std::size_t> HeaderOffset(ara::core::model::ModelMavlinkField::Role role) const;
  /// Długość reprezentacji SOME/IP (bez pól nagłówka; dla stringa najwyższego poziomu: max).
  std::size_t someip_len() const { return someip_len_; }

  /// Zwraca pełny (nieobcięty) payload MAVLink.
  ara::core::Result<std::vector<std::uint8_t>> Encode(
      const MessageHeader& header, const std::vector<std::uint8_t>& someip) const;

  /// Przyjmuje payload jak z przewodu (v2: może być obcięty).
  ara::core::Result<DecodedMessage> Decode(
      const std::vector<std::uint8_t>& payload) const;

 private:
  struct Slot {
    ara::core::model::ModelMavlinkField field;
    std::size_t elem_size{1};     // rozmiar elementu (char: 1)
    std::size_t total_size{1};    // elem_size * max(1, array_len)
    std::size_t wire_offset{0};   // w payloadzie MAVLink
    std::size_t someip_offset{0};  // w reprezentacji SOME/IP (tylko payload)
  };
  std::vector<Slot> slots_;
  std::uint32_t msg_id_;
  std::uint8_t crc_extra_;
  std::string name_;
  std::size_t full_len_{0};
  std::size_t someip_len_{0};
  bool raw_string_{false};
};

/// crc_extra wg algorytmu mavgen (do testów zgodności z wygenerowanym dialektem).
std::uint8_t ComputeCrcExtra(const ara::core::model::ModelMavlinkMessage& msg);

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_MAVLINK_CODEC_H_
