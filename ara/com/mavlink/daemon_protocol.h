/**
 * Copyright (c) 2024
 *
 * @file daemon_protocol.h
 * @brief Protokół IPC aplikacja <-> MavlinkDaemon (rev. 10).
 *
 * Aplikacje (bindingi `on: mavlink`) są cienkimi klientami daemona
 * (`platform/common/mavlink_daemon`): rejestrują instancje usług i przekazują
 * wiadomości MAVLink; daemon posiada łącza, kolejki, podpis, HEARTBEAT i dedup.
 *
 * TRANSPORT: gniazdo unix `SOCK_SEQPACKET` (`daemon_ipc.h`). Jeden komunikat
 * protokołu = jeden pakiet (granice komunikatów bez własnego ramkowania),
 * kolejność i niezawodność jak w strumieniu, a zamknięcie połączenia przez
 * aplikację (także po jej awarii) daemon widzi od razu i sprząta rejestracje.
 * Mechanizm IPC someip_demon (`bindings/common/socket`, jedno połączenie na
 * żądanie, bez stanu) tego nie zapewnia, a daemon potrzebuje połączenia ze stanem.
 *
 * WSPÓŁBIEŻNOŚĆ / MODEL:
 *  - jedno połączenie = jedna aplikacja; na połączeniu można zarejestrować wiele
 *    instancji (`RegisterInstance`, każda z własnym `handle` nadanym przez
 *    aplikację, unikalnym w obrębie połączenia);
 *  - rejestracja jest potwierdzana `RegisterReply`; do czasu potwierdzenia
 *    aplikacja nie wysyła wiadomości instancji;
 *  - po `RegisterReply{kOk}` daemon wysyła `LinkStatus` dla każdego łącza instancji
 *    (stan początkowy), potem przy każdej zmianie (gotowe/brak);
 *  - restart daemona = zamknięcie połączenia; aplikacja łączy się ponownie
 *    i rejestruje WSZYSTKIE instancje od nowa (stan nie jest odtwarzany przez daemon).
 *
 * TREŚĆ WIADOMOŚCI: aplikacja koduje/dekoduje payload (`MessageCodec`), daemon
 * operuje na PEŁNYM (nieobciętym) payloadzie MAVLink wg opisu `MessageDesc`
 * (offsety pól nagłówka: seq/instance/target/status). Daemon sam obcina zera v2
 * przy nadawaniu i dopełnia je przy odbiorze.
 *
 * KOD BŁĘDU: `Decode` zwraca ComErrc::kFieldValueIsNotValid dla komunikatu
 * zniekształconego albo przekraczającego limity poniżej.
 *
 * UWAGA: zmiana układu komunikatów = zmiana `kProtocolVersion`.
 */
#ifndef ARA_COM_MAVLINK_DAEMON_PROTOCOL_H_
#define ARA_COM_MAVLINK_DAEMON_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <span>  // NOLINT
#include <string>
#include <variant>
#include <vector>

#include "ara/core/result.h"

namespace ara {
namespace com {
namespace mavlink {
namespace daemon {

/// Wersja protokołu (pierwsze pole `RegisterInstance`); inna = `kBadVersion`.
constexpr std::uint16_t kProtocolVersion = 1;
/// Domyślna ścieżka gniazda daemona (nadpisywana w configu daemona/aplikacji).
constexpr const char* kDefaultSocketPath = "/run/srp_mavlink_daemon.sock";

// Limity (egzekwowane przez Encode i Decode).
constexpr std::size_t kMaxMessageSize = 16384;  ///< największy pakiet na gnieździe
constexpr std::size_t kMaxStringLen = 255;      ///< nazwy, teksty (długość w u8)
constexpr std::size_t kMaxLinksPerInstance = 8;
constexpr std::size_t kMaxElements = 64;        ///< elementów (event/metoda) na instancję
constexpr std::size_t kMaxPayload = 255;        ///< payload MAVLink

/// `MessageDesc::*_offset` == kNoOffset: pole nie występuje w wiadomości.
constexpr std::uint8_t kNoOffset = 0xFF;

/// Typ komunikatu (pierwszy bajt pakietu).
enum class MsgType : std::uint8_t {
  kRegisterInstance = 1,  ///< app -> daemon
  kRegisterReply = 2,     ///< daemon -> app
  kUnregister = 3,        ///< app -> daemon
  kEvent = 4,             ///< app -> daemon: event do wysłania (skeleton)
  kRequest = 5,           ///< app -> daemon: REQ metody (proxy)
  kResponse = 6,          ///< app -> daemon: RESP metody (skeleton)
  kSendError = 7,         ///< daemon -> app: REQ nie został przyjęty do nadania
  kEventIn = 8,           ///< daemon -> app: event odebrany (proxy)
  kRequestIn = 9,         ///< daemon -> app: REQ odebrany (skeleton)
  kResponseIn = 10,       ///< daemon -> app: RESP odebrana (proxy)
  kLinkStatus = 11        ///< daemon -> app: łącze gotowe / brak
};

/// Rola instancji w usłudze.
enum class Role : std::uint8_t {
  kProvide = 0,  ///< skeleton: nadaje eventy i RESP, odbiera REQ
  kRequire = 1   ///< proxy: odbiera eventy i RESP, nadaje REQ
};

/// Opis jednej wiadomości MAVLink (event, REQ albo RESP). Offsety wskazują miejsce
/// pola nagłówka w PEŁNYM payloadzie (układ wg MessageCodec); seq i instance mają
/// 2 B little-endian, pozostałe 1 B.
struct MessageDesc {
  std::uint32_t msg_id{0};
  std::uint8_t crc_extra{0};
  std::uint8_t full_len{0};  ///< pełna długość payloadu (<= 255)
  std::uint8_t seq_offset{kNoOffset};
  std::uint8_t instance_offset{kNoOffset};
  std::uint8_t target_system_offset{kNoOffset};
  std::uint8_t target_component_offset{kNoOffset};
  std::uint8_t status_offset{kNoOffset};
  std::string name;  ///< do logów
};

/// Element usługi (event albo metoda).
struct ElementDesc {
  std::uint16_t id{0};          ///< id SOME/IP (event: >= 0x8000)
  bool is_event{false};
  /// false = element wykluczony (`exclude`) albo wyłączony: nie nadaje, odbiór
  /// odrzucany (REQ -> RESP status=2 jeśli ma RESP).
  bool available{true};
  bool has_resp{false};
  std::uint32_t timeout_ms{1000};     ///< metoda: po tym czasie daemon porzuca REQ czekający w FIFO
  std::uint32_t min_interval_ms{0};   ///< event: limit częstotliwości (max_rate_hz), 0 = bez limitu
  MessageDesc req;                    ///< event: wiadomość eventu; metoda: REQ
  MessageDesc resp;                   ///< tylko gdy has_resp
};

/// Rejestracja instancji usługi (provide/require).
struct RegisterInstance {
  std::uint16_t protocol_version{kProtocolVersion};
  std::uint32_t handle{0};  ///< nadaje aplikacja, unikalny w obrębie połączenia
  Role role{Role::kProvide};
  std::string com_id;       ///< nazwa usługi (do logów i wykrywania konfliktów)
  std::uint16_t instance{0};
  /// require: adresat REQ (0 = rozgłoszenie).
  std::uint8_t target_system{0};
  std::uint8_t target_component{0};
  /// require: dozwolone źródło eventów (0 = dowolne).
  std::uint8_t source_system{0};
  std::uint8_t source_component{0};
  std::uint32_t dedup_ttl_ms{10000};
  std::vector<std::string> links;  ///< łącza z configu daemona (redundancja)
  std::vector<ElementDesc> elements;
};

enum class RegisterStatus : std::uint8_t {
  kOk = 0,
  kBadVersion = 1,        ///< `protocol_version` != wersja daemona
  kInvalid = 2,           ///< niespójny opis (limity, offsety, brak elementów)
  kUnknownLink = 3,       ///< łącze nie istnieje w configu daemona
  kConflictInstance = 4,  ///< (com_id, instance, role) już zarejestrowane przez inną instancję
  kConflictMsgId = 5,     ///< msg_id zajęty przez inną definicję (nazwa/crc_extra) albo inną usługę
  kDuplicateHandle = 6    ///< `handle` już użyty na tym połączeniu
};

struct RegisterReply {
  std::uint32_t handle{0};
  RegisterStatus status{RegisterStatus::kOk};
  std::uint8_t system_id{0};     ///< system_id / component_id daemona (1 na CPU)
  std::uint8_t component_id{0};
  std::string text;              ///< czytelny powód odrzucenia
};

/// Wyrejestrowanie instancji (bez odpowiedzi). Rozłączenie ma ten sam skutek dla
/// wszystkich instancji połączenia.
struct Unregister {
  std::uint32_t handle{0};
};

/// Wspólna treść komunikatów app -> daemon niosących wiadomość MAVLink.
struct ElementPayload {
  std::uint32_t handle{0};
  std::uint16_t element_id{0};
  std::vector<std::uint8_t> payload;  ///< pełny payload MAVLink wiadomości `req` (event/REQ) albo `resp`
};
struct Event : ElementPayload {};     ///< kEvent (skeleton)
struct Request : ElementPayload {};   ///< kRequest (proxy)
struct Response : ElementPayload {};  ///< kResponse (skeleton)

enum class SendErrc : std::uint8_t {
  kUnknownElement = 1,  ///< nieznany handle / element / niezgodna długość payloadu
  kUnavailable = 2,     ///< element wykluczony
  kNoLink = 3,          ///< żadne łącze instancji nie jest gotowe (natychmiast, bez kolejkowania)
  kQueueFull = 4        ///< FIFO REQ/RESP pełne na wszystkich łączach instancji
};

/// Daemon odrzucił REQ (`seq` odczytany z payloadu). Błąd eventów i RESP: tylko
/// licznik po stronie daemona (bez komunikatu).
struct SendError {
  std::uint32_t handle{0};
  std::uint16_t element_id{0};
  std::uint16_t seq{0};
  SendErrc code{SendErrc::kUnknownElement};
};

/// Wspólna treść komunikatów daemon -> app niosących odebraną wiadomość. Daemon
/// zrobił już: CRC, podpis, filtr łącza instancji, target/źródło, dedup. `payload`
/// jest PEŁNY (dopełniony zerami) — gotowy do `MessageCodec::Decode`.
struct Delivery {
  std::uint32_t handle{0};
  std::uint16_t element_id{0};
  std::uint8_t sender_system{0};
  std::uint8_t sender_component{0};
  std::string link;  ///< łącze, którym przyszła ramka
  std::vector<std::uint8_t> payload;
};
struct EventIn : Delivery {};     ///< proxy
struct RequestIn : Delivery {};   ///< skeleton
struct ResponseIn : Delivery {};  ///< proxy

/// Stan łącza instancji (gotowe = transport otwarty, można nadawać).
struct LinkStatus {
  std::uint32_t handle{0};
  std::string link;
  bool ready{false};
};

using Message = std::variant<RegisterInstance, RegisterReply, Unregister, Event, Request, Response,
                             SendError, EventIn, RequestIn, ResponseIn, LinkStatus>;

/// Typ komunikatu (indeks alternatywy `Message` + 1 == wartość `MsgType`).
MsgType TypeOf(const Message& m) noexcept;

/// Serializacja do jednego pakietu (little-endian, pierwszy bajt = MsgType).
/// Błąd: przekroczenie limitów.
ara::core::Result<std::vector<std::uint8_t>> Encode(const Message& m);

/// Parsowanie jednego pakietu; bajty nadmiarowe = błąd.
ara::core::Result<Message> Decode(std::span<const std::uint8_t> packet);

/// Kontrola spójności opisu instancji (limity, offsety w granicach `full_len`,
/// niepuste elementy); pusty tekst = poprawny. Wołane przez daemon i przez
/// aplikację przed wysłaniem.
std::string Validate(const RegisterInstance& r);

}  // namespace daemon
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_DAEMON_PROTOCOL_H_
