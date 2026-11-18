/**
 * @file mavlink_model_item.h
 * @brief Model komunikacji MAVLink (generowany do initialization.cc).
 *
 * Tylko model danych instancji usługi na `on: mavlink`. Łącza, system_id/component_id,
 * podpis i HEARTBEAT należą do MavlinkDaemon (jego config); aplikacja zna tylko nazwy łączy.
 *
 * @copyright Copyright (c) 2024
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

/// Pole wiadomości MAVLink (z generatora). Wiadomość = nagłówek (role != kPayload)
/// + pola payloadu w kolejności z modelu; układ na przewodzie wylicza kodek.
struct ModelMavlinkField {
  enum class Type : std::uint8_t {
    kU8, kI8, kU16, kI16, kU32, kI32, kU64, kI64, kF32, kF64, kChar
  };
  enum class Role : std::uint8_t {
    kPayload, kSeq, kInstance, kTargetSystem, kTargetComponent, kStatus
  };
  std::string name;
  Type type{Type::kU8};
  std::uint16_t array_len{0};  ///< kChar: długość tablicy char[N]
  Role role{Role::kPayload};
  bool big_endian{false};  ///< endian reprezentacji SOME/IP tego pola
};

struct ModelMavlinkMessage {
  std::uint32_t msg_id{0};
  std::uint8_t crc_extra{0};
  std::string name;
  std::vector<ModelMavlinkField> fields;
};

/// Element usługi (event albo metoda) z danymi potrzebnymi w runtime.
struct ModelMavlinkElement {
  std::uint16_t id{0};  ///< id SOME/IP (event: >= 0x8000)
  bool is_event{false};
  bool enabled{false};  ///< false = poza MAVLinkiem ("mavlink": false/brak bloku)
  std::string name;
  ModelMavlinkMessage req;  ///< event: wiadomość eventu; metoda: REQ
  bool has_resp{false};
  ModelMavlinkMessage resp;
  std::uint32_t timeout_ms{1000};
  double max_rate_hz{0.0};  ///< z service.json (0 = bez limitu)
};

/// Opcje instancji usługi (wpis on=mavlink): wyłączone elementy i nadpisane limity.
struct ModelMavlinkOptions {
  /// Tożsamość usługi dla MavlinkDaemona: wspólna dla WSZYSTKICH instancji i ról tej usługi (daemon
  /// dopuszcza ten sam msg_id tylko w obrębie jednej usługi, a konflikt instancji liczy per usługa).
  /// Pusta = `com_id_` instancji (testy).
  std::string service_name;
  std::vector<std::string> exclude;                // eventy/metody poza radiem
  std::map<std::string, double> max_rate_hz;       // nadpisanie limitu (0 = bez limitu)
  std::uint32_t dedup_ttl_ms{10000};    // okno dedup kopii z wielu łączy
  std::vector<ModelMavlinkElement> elements;  // wszystkie elementy usługi
  std::uint8_t source_system_id{0};     // require: 0 = dowolny nadawca
  std::uint8_t source_component_id{0};  // require: 0 = dowolny komponent
};

/// Usługa wystawiona/wymagana przez MAVLink: nazwy łączy daemona (redundancja).
class ModelMavlink {
 public:
  enum Direction { kIn = 0, kOut = 1 };
  const std::string com_id_;
  const std::uint16_t instance_;
  const std::uint8_t major_version_;
  const std::uint32_t minor_version_;
  const std::uint8_t target_system_id_;
  const std::uint8_t target_component_id_;
  const std::vector<std::string> links_;
  const ModelMavlinkOptions options_;
  const Direction dir_;

  ModelMavlink(const std::string& com_id, const std::uint16_t instance,
               const std::uint8_t major_version, const std::uint32_t minor_version,
               const std::uint8_t target_system_id, const std::uint8_t target_component_id,
               const std::vector<std::string>& links, const ModelMavlinkOptions& options,
               const Direction dir)
      : com_id_{com_id},
        instance_{instance},
        major_version_{major_version},
        minor_version_{minor_version},
        target_system_id_{target_system_id},
        target_component_id_{target_component_id},
        links_{links},
        options_{options},
        dir_{dir} {}

  static core::Result<ModelMavlink> CreatProxyItem(
      const std::string& com_id, const std::uint16_t instance, const std::uint8_t major_version,
      const std::uint32_t minor_version, const std::uint8_t target_system_id,
      const std::uint8_t target_component_id, const std::vector<std::string>& links,
      const ModelMavlinkOptions& options = {}) {
    return ModelMavlink(com_id, instance, major_version, minor_version, target_system_id,
                        target_component_id, links, options, kIn);
  }
  static core::Result<ModelMavlink> CreatSkeletonItem(
      const std::string& com_id, const std::uint16_t instance, const std::uint8_t major_version,
      const std::uint32_t minor_version, const std::uint8_t target_system_id,
      const std::uint8_t target_component_id, const std::vector<std::string>& links,
      const ModelMavlinkOptions& options = {}) {
    return ModelMavlink(com_id, instance, major_version, minor_version, target_system_id,
                        target_component_id, links, options, kOut);
  }
};
}  // namespace model
}  // namespace core
}  // namespace ara

#endif  // ARA_CORE_MODEL_MAVLINK_MODEL_ITEM_H_
