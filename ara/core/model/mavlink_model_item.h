/**
 * @file mavlink_model_item.h
 * @brief MAVLink communication model (generated into initialization.cc).
 * @copyright Copyright (c) 2024
 *
 * Data model of a service instance on `on: mavlink` only. Links, system_id/component_id,
 * signing and HEARTBEAT belong to the MavlinkDaemon (its config); the application only knows
 * link names.
 *
 * Factory signature: CreatProxyItem/CreatSkeletonItem(com_id, instance, target_system_id,
 * target_component_id, links, options).
 */
#ifndef ARA_CORE_MODEL_MAVLINK_MODEL_ITEM_H_
#define ARA_CORE_MODEL_MAVLINK_MODEL_ITEM_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ara/core/result.h"
namespace ara {
namespace core {
namespace model {

/// Field of a MAVLink message (from the generator). A message = header fields (role !=
/// kPayload) + payload fields in model order; the codec computes the wire layout.
struct ModelMavlinkField {
  enum class Type : std::uint8_t {
    kU8, kI8, kU16, kI16, kU32, kI32, kU64, kI64, kF32, kF64, kChar
  };
  enum class Role : std::uint8_t {
    kPayload, kSeq, kInstance, kTargetSystem, kTargetComponent, kStatus
  };
  std::string name;
  Type type{Type::kU8};
  std::uint16_t array_len{0};  ///< kChar: length of the char[N] array
  Role role{Role::kPayload};
  bool big_endian{false};  ///< byte order of this field in its SOME/IP representation
};

struct ModelMavlinkMessage {
  std::uint32_t msg_id{0};
  std::uint8_t crc_extra{0};
  std::string name;
  std::vector<ModelMavlinkField> fields;
};

/// Service element (event or method) with the data needed at runtime.
struct ModelMavlinkElement {
  std::uint16_t id{0};  ///< SOME/IP id (event: >= 0x8000)
  bool is_event{false};
  bool enabled{false};  ///< false = not on MAVLink ("mavlink": false / no block)
  std::string name;
  ModelMavlinkMessage req;  ///< event: the event message; method: REQ
  bool has_resp{false};
  ModelMavlinkMessage resp;
  std::uint32_t timeout_ms{1000};
  double max_rate_hz{0.0};  ///< from service.json (0 = no limit)
};

/// Options of a service instance (on=mavlink entry): excluded elements and overridden limits.
struct ModelMavlinkOptions {
  /// Service identity for the MavlinkDaemon: shared by ALL instances and roles of the service (the
  /// daemon allows the same msg_id only within one service and detects instance conflicts per
  /// service). Empty = the instance `com_id_` (tests).
  std::string service_name;
  std::vector<std::string> exclude;                // events/methods kept off the radio
  std::map<std::string, double> max_rate_hz;       // limit override (0 = no limit)
  std::uint32_t dedup_ttl_ms{10000};    // dedup window for copies from several links
  std::vector<ModelMavlinkElement> elements;  // all elements of the service
  std::uint8_t source_system_id{0};     // require: 0 = any sender
  std::uint8_t source_component_id{0};  // require: 0 = any component
};

/// Service provided/required over MAVLink: names of daemon links (redundancy).
class ModelMavlink {
 public:
  enum Direction { kIn = 0, kOut = 1 };
  const std::string com_id_;
  const std::uint16_t instance_;
  const std::uint8_t target_system_id_;
  const std::uint8_t target_component_id_;
  const std::vector<std::string> links_;
  const ModelMavlinkOptions options_;
  const Direction dir_;

  ModelMavlink(const std::string& com_id, const std::uint16_t instance,
               const std::uint8_t target_system_id, const std::uint8_t target_component_id,
               const std::vector<std::string>& links, const ModelMavlinkOptions& options,
               const Direction dir)
      : com_id_{com_id},
        instance_{instance},
        target_system_id_{target_system_id},
        target_component_id_{target_component_id},
        links_{links},
        options_{options},
        dir_{dir} {}

  static core::Result<ModelMavlink> CreatProxyItem(
      const std::string& com_id, const std::uint16_t instance,
      const std::uint8_t target_system_id, const std::uint8_t target_component_id,
      const std::vector<std::string>& links, const ModelMavlinkOptions& options = {}) {
    return ModelMavlink(com_id, instance, target_system_id, target_component_id, links, options,
                        kIn);
  }
  static core::Result<ModelMavlink> CreatSkeletonItem(
      const std::string& com_id, const std::uint16_t instance,
      const std::uint8_t target_system_id, const std::uint8_t target_component_id,
      const std::vector<std::string>& links, const ModelMavlinkOptions& options = {}) {
    return ModelMavlink(com_id, instance, target_system_id, target_component_id, links, options,
                        kOut);
  }
};
}  // namespace model
}  // namespace core
}  // namespace ara

#endif  // ARA_CORE_MODEL_MAVLINK_MODEL_ITEM_H_
