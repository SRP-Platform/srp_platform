/**
 * Copyright (c) 2024
 *
 * @file mavlink_codec.h
 * @brief Message codec: SOME/IP bytes (as produced by the platform converters)
 * <-> MAVLink payload according to the field description of the model (ModelMavlinkMessage).
 *
 * The codec is interpretive: it is driven at runtime by the `ModelMavlinkMessage` instead of
 * generated per message. This is a deliberate design decision — a single small piece of code
 * replaces a generated C++ codec for every message at the cost of a table walk per frame.
 *
 * MAVLink wire layout: fields sorted by decreasing element size (stable, like mavgen), integers
 * little-endian. SOME/IP representation: payload fields concatenated in model order, without
 * padding; byte order per field (little by default — this is what the platform converters do).
 * A `char[N]` inside a struct = N bytes padded with zeros; a single top-level string = raw
 * text bytes (up to the first NUL).
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

/// Header fields of a message (not part of the SOME/IP payload).
struct MessageHeader {
  std::uint16_t seq{0};
  std::uint16_t instance{0};
  std::uint8_t target_system{0};
  std::uint8_t target_component{0};
  std::uint8_t status{0};
};

struct DecodedMessage {
  MessageHeader header;
  std::vector<std::uint8_t> someip;  ///< payload in the SOME/IP representation
};

class MessageCodec {
 public:
  explicit MessageCodec(const ara::core::model::ModelMavlinkMessage& msg);

  std::uint32_t msg_id() const { return msg_id_; }
  std::uint8_t crc_extra() const { return crc_extra_; }
  const std::string& name() const { return name_; }
  /// Full MAVLink payload length (v2 zero truncation not applied).
  std::size_t full_payload_len() const { return full_len_; }
  /// Offset of a header field (seq/instance/target/status) in the full MAVLink payload; nullopt
  /// = the message has no such field.
  std::optional<std::size_t> HeaderOffset(ara::core::model::ModelMavlinkField::Role role) const;
  /// Length of the SOME/IP representation (without header fields; top-level string: maximum).
  std::size_t someip_len() const { return someip_len_; }

  /// Returns the full (untruncated) MAVLink payload.
  ara::core::Result<std::vector<std::uint8_t>> Encode(
      const MessageHeader& header, const std::vector<std::uint8_t>& someip) const;

  /// Accepts a payload as on the wire (v2: may be truncated).
  ara::core::Result<DecodedMessage> Decode(
      const std::vector<std::uint8_t>& payload) const;

 private:
  struct Slot {
    ara::core::model::ModelMavlinkField field;
    std::size_t elem_size{1};     // element size (char: 1)
    std::size_t total_size{1};    // elem_size * max(1, array_len)
    std::size_t wire_offset{0};   // in the MAVLink payload
    std::size_t someip_offset{0};  // in the SOME/IP representation (payload only)
  };
  std::vector<Slot> slots_;
  std::uint32_t msg_id_;
  std::uint8_t crc_extra_;
  std::string name_;
  std::size_t full_len_{0};
  std::size_t someip_len_{0};
  bool raw_string_{false};
};

/// crc_extra per the mavgen algorithm (for conformance tests against a generated dialect).
std::uint8_t ComputeCrcExtra(const ara::core::model::ModelMavlinkMessage& msg);

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_MAVLINK_CODEC_H_
