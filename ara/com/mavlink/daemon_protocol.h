/**
 * Copyright (c) 2024
 *
 * @file daemon_protocol.h
 * @brief IPC protocol between applications and the MavlinkDaemon.
 *
 * Applications (`on: mavlink` bindings) are thin clients of the daemon
 * (`platform/common/mavlink_daemon`): they register service instances and pass MAVLink
 * messages through; the daemon owns the links, queues, signing, HEARTBEAT and dedup.
 *
 * TRANSPORT: unix `SOCK_SEQPACKET` socket (`daemon_ipc.h`). One protocol message = one
 * packet (no own framing needed), ordering and reliability like a stream, and the daemon
 * sees the connection closing at once (also after an application crash) and cleans up the
 * registrations. The someip_demon IPC (`bindings/common/socket`, one connection per request,
 * stateless) cannot provide this, and the daemon needs stateful connections.
 *
 * MODEL:
 *  - one connection = one application; many instances can be registered on it
 *    (`RegisterInstance`, each with an application-chosen `handle` unique per connection);
 *  - a registration is confirmed by `RegisterReply`; until then the application sends no
 *    messages of the instance;
 *  - after `RegisterReply{kOk}` the daemon sends a `LinkStatus` for every link of the instance
 *    (initial state), then on every change (ready/not ready);
 *  - a daemon restart closes the connection; the application reconnects and registers ALL
 *    instances again (the daemon does not restore state).
 *
 * MESSAGE CONTENT: the application encodes/decodes the payload (`MessageCodec`); the daemon
 * works on the FULL (untruncated) MAVLink payload as described by `MessageDesc` (offsets of
 * the header fields seq/instance/target/status). The daemon truncates v2 zeros itself when
 * transmitting and pads them when receiving.
 *
 * ERRORS: `Decode` returns ComErrc::kFieldValueIsNotValid for a malformed message or one that
 * exceeds the limits below.
 *
 * The message layout is versioned by `kProtocolVersion`: any layout change bumps it. Version 2
 * merged the three element messages (event / request / response) into `ElementPayload` and the
 * three deliveries into `Delivery`, each with a `kind` byte.
 */
#ifndef ARA_COM_MAVLINK_DAEMON_PROTOCOL_H_
#define ARA_COM_MAVLINK_DAEMON_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <span>  // NOLINT
#include <string>
#include <variant>
#include <vector>

#include "ara/com/mavlink/mavlink_codec.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {
namespace daemon {

/// Protocol version (first field of `RegisterInstance`); a different one = `kBadVersion`.
constexpr std::uint16_t kProtocolVersion = 2;
/// Default daemon socket path (overridden in the daemon / application config).
constexpr const char* kDefaultSocketPath = "/run/srp_mavlink_daemon.sock";

// Limits (enforced by Encode and Decode).
constexpr std::size_t kMaxMessageSize = 16384;  ///< largest packet on the socket
constexpr std::size_t kMaxStringLen = 255;      ///< names, texts (length in a u8)
constexpr std::size_t kMaxLinksPerInstance = 8;
constexpr std::size_t kMaxElements = 64;        ///< elements (event/method) per instance
constexpr std::size_t kMaxPayload = 255;        ///< MAVLink payload

/// `MessageDesc::*_offset` == kNoOffset: the field is not present in the message.
constexpr std::uint8_t kNoOffset = 0xFF;

/// Message type (first byte of a packet); equals the index of the `Message` alternative + 1.
enum class MsgType : std::uint8_t {
  kRegisterInstance = 1,  ///< app -> daemon
  kRegisterReply = 2,     ///< daemon -> app
  kUnregister = 3,        ///< app -> daemon
  kElement = 4,           ///< app -> daemon: event / REQ / RESP to transmit
  kSendError = 5,         ///< daemon -> app: a REQ was not accepted for transmission
  kDelivery = 6,          ///< daemon -> app: event / REQ / RESP received
  kLinkStatus = 7         ///< daemon -> app: link ready / not ready
};

/// Role of an instance in a service.
enum class Role : std::uint8_t {
  kProvide = 0,  ///< skeleton: transmits events and RESP, receives REQ
  kRequire = 1   ///< proxy: receives events and RESP, transmits REQ
};

/// Kind of the message carried by `ElementPayload` / `Delivery`.
enum class ElementKind : std::uint8_t { kEvent = 0, kRequest = 1, kResponse = 2 };

/// Description of one MAVLink message (event, REQ or RESP). Offsets point at the header field
/// inside the FULL payload (layout per MessageCodec); seq and instance are 2 B little-endian,
/// the others 1 B.
struct MessageDesc {
  std::uint32_t msg_id{0};
  std::uint8_t crc_extra{0};
  std::uint8_t full_len{0};  ///< full payload length (<= 255)
  std::uint8_t seq_offset{kNoOffset};
  std::uint8_t instance_offset{kNoOffset};
  std::uint8_t target_system_offset{kNoOffset};
  std::uint8_t target_component_offset{kNoOffset};
  std::uint8_t status_offset{kNoOffset};
  std::string name;  ///< for logs
};

/// Service element (event or method).
struct ElementDesc {
  std::uint16_t id{0};          ///< SOME/IP id (event: >= 0x8000)
  bool is_event{false};
  /// false = element excluded (`exclude`) or disabled: does not transmit, reception is
  /// rejected (REQ -> RESP status=2 if it has a RESP).
  bool available{true};
  bool has_resp{false};
  std::uint32_t timeout_ms{1000};     ///< method: the daemon drops a REQ waiting in the FIFO after this time
  std::uint32_t min_interval_ms{0};   ///< event: rate limit (max_rate_hz), 0 = no limit
  MessageDesc req;                    ///< event: the event message; method: REQ
  MessageDesc resp;                   ///< only when has_resp
};

/// Registration of a service instance (provide/require).
struct RegisterInstance {
  std::uint16_t protocol_version{kProtocolVersion};
  std::uint32_t handle{0};  ///< chosen by the application, unique per connection
  Role role{Role::kProvide};
  std::string com_id;       ///< service name (for logs and conflict detection)
  std::uint16_t instance{0};
  /// require: addressee of REQ (0 = broadcast).
  std::uint8_t target_system{0};
  std::uint8_t target_component{0};
  /// require: allowed source of events (0 = any).
  std::uint8_t source_system{0};
  std::uint8_t source_component{0};
  std::uint32_t dedup_ttl_ms{10000};
  std::vector<std::string> links;  ///< links from the daemon config (redundancy)
  std::vector<ElementDesc> elements;
};

enum class RegisterStatus : std::uint8_t {
  kOk = 0,
  kBadVersion = 1,        ///< `protocol_version` != the daemon's version
  kInvalid = 2,           ///< inconsistent description (limits, offsets, no elements)
  kUnknownLink = 3,       ///< the link does not exist in the daemon config
  kConflictInstance = 4,  ///< (com_id, instance, role) already registered by another instance
  kConflictMsgId = 5,     ///< msg_id taken by a different definition (name/crc_extra) or service
  kDuplicateHandle = 6    ///< `handle` already used on this connection
};

struct RegisterReply {
  std::uint32_t handle{0};
  RegisterStatus status{RegisterStatus::kOk};
  std::uint8_t system_id{0};     ///< daemon system_id / component_id (one per CPU)
  std::uint8_t component_id{0};
  std::string text;              ///< human-readable rejection reason
};

/// Unregisters an instance (no reply). Disconnecting has the same effect for all instances of
/// the connection.
struct Unregister {
  std::uint32_t handle{0};
};

/// app -> daemon: a message of `kind` to transmit (event / REQ / RESP).
struct ElementPayload {
  ElementKind kind{ElementKind::kEvent};
  std::uint32_t handle{0};
  std::uint16_t element_id{0};
  std::vector<std::uint8_t> payload;  ///< full MAVLink payload of the `req` (event/REQ) or `resp` message
};

enum class SendErrc : std::uint8_t {
  kUnknownElement = 1,  ///< unknown handle / element / payload length mismatch
  kUnavailable = 2,     ///< element excluded
  kNoLink = 3,          ///< no link of the instance is ready (at once, no queueing)
  kQueueFull = 4        ///< REQ/RESP FIFO full on every link of the instance
};

/// The daemon rejected a REQ (`seq` read from the payload). Errors of events and RESP are only a
/// counter on the daemon side (no message).
struct SendError {
  std::uint32_t handle{0};
  std::uint16_t element_id{0};
  std::uint16_t seq{0};
  SendErrc code{SendErrc::kUnknownElement};
};

/// daemon -> app: a received message of `kind`. The daemon already did: CRC, signature, instance
/// link filter, target/source, dedup. `payload` is FULL (zero-padded) — ready for
/// `MessageCodec::Decode`.
struct Delivery {
  ElementKind kind{ElementKind::kEvent};
  std::uint32_t handle{0};
  std::uint16_t element_id{0};
  std::uint8_t sender_system{0};
  std::uint8_t sender_component{0};
  std::string link;  ///< link the frame arrived on
  std::vector<std::uint8_t> payload;
};

/// State of an instance link (ready = transport open, can transmit).
struct LinkStatus {
  std::uint32_t handle{0};
  std::string link;
  bool ready{false};
};

using Message = std::variant<RegisterInstance, RegisterReply, Unregister, ElementPayload, SendError,
                             Delivery, LinkStatus>;

/// Reads / writes the header fields of a FULL payload at the offsets of `d`; absent fields read
/// as 0 and are not written. `full` must be at least `d.full_len` long.
MessageHeader ReadHeader(const MessageDesc& d, std::span<const std::uint8_t> full);
void WriteHeader(const MessageDesc& d, const MessageHeader& h, std::vector<std::uint8_t>* full);

/// Type of a message (index of the `Message` alternative + 1 == `MsgType` value).
MsgType TypeOf(const Message& m) noexcept;

/// Serialises into one packet (little-endian, first byte = MsgType). Error: limits exceeded.
ara::core::Result<std::vector<std::uint8_t>> Encode(const Message& m);

/// Parses one packet; excess bytes are an error.
ara::core::Result<Message> Decode(std::span<const std::uint8_t> packet);

/// Consistency check of an instance description (limits, offsets within `full_len`, non-empty
/// elements); empty text = valid. Called by the daemon and by the application before sending.
std::string Validate(const RegisterInstance& r);

}  // namespace daemon
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_DAEMON_PROTOCOL_H_
