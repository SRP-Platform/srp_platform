/**
 * @file mavlink_peer.cc
 * @copyright Copyright (c) 2024
 * @brief Peer testowy na PRAWDZIWYM runtime MAVLink (LinkManager + bindingi), protokol: tests/mavlink/README.md.
 *
 * Model (elementy, ID, kodek, crc_extra) pochodzi z wygenerowanego initialization.cc (peer_model/), a dane
 * konfiguracyjne procesu (urzadzenia pty, klucze, instancje, wykluczenia, limity) przychodza z polecenia `start`.
 * Dla kazdej instancji tworzymy skeleton i proxy (jeden proces = obie role).
 * stdin: linie JSON; stdout: odpowiedzi i zdarzenia (patrz tests/mavlink/README.md, Protokol peera).
 */
#include <unistd.h>

#include <chrono>  // NOLINT
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>  // NOLINT
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/com_error_domain.h"
#include "ara/com/mavlink/link_manager.h"
#include "ara/com/mavlink/link_transport_registry.h"
#include "ara/com/mavlink/serial_transport.h"
#include "ara/core/model_db.h"
#include "nlohmann/json.hpp"

namespace ara {
namespace core {
Result<void> InitializeComDb(ModelDataBase& db_) noexcept;  // NOLINT(runtime/references) generowane
}  // namespace core
}  // namespace ara

namespace {
using json = nlohmann::json;
using ara::com::mavlink::LinkManager;
using ara::com::mavlink::LinkTransportRegistry;
using ara::com::mavlink::MavlinkEndpoint;
using ara::core::model::ModelMavlink;
using ara::core::model::ModelMavlinkElement;
using ara::core::model::ModelMavlinkField;
using ara::core::model::ModelMavlinkLink;
using ara::core::model::ModelMavlinkMessage;
using ara::core::model::ModelMavlinkOptions;
using Type = ModelMavlinkField::Type;
using Role = ModelMavlinkField::Role;

std::mutex g_out_mutex;
void Send(const json& j) {
  std::lock_guard<std::mutex> lock{g_out_mutex};
  std::cout << j.dump() << "\n";
  std::cout.flush();
}

std::size_t TypeSize(Type t) {
  switch (t) {
    case Type::kU8: case Type::kI8: case Type::kChar: return 1;
    case Type::kU16: case Type::kI16: return 2;
    case Type::kU32: case Type::kI32: case Type::kF32: return 4;
    default: return 8;
  }
}

void PutInt(std::vector<std::uint8_t>* out, std::uint64_t v, std::size_t n, bool big) {
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t shift = big ? (n - 1 - i) * 8 : i * 8;
    out->push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
  }
}

std::uint64_t GetInt(const std::vector<std::uint8_t>& b, std::size_t off, std::size_t n, bool big) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t shift = big ? (n - 1 - i) * 8 : i * 8;
    v |= static_cast<std::uint64_t>(b[off + i]) << shift;
  }
  return v;
}

/// Wartosci JSON -> bajty SOME/IP (pola payloadu w kolejnosci modelu, endian wg pola, char[N] dopelniony zerami).
std::vector<std::uint8_t> Pack(const ModelMavlinkMessage& msg, const json& values) {
  std::vector<std::uint8_t> out;
  for (const auto& f : msg.fields) {
    if (f.role != Role::kPayload) {
      continue;
    }
    if (f.type == Type::kChar) {
      std::string s = values.contains(f.name) && values[f.name].is_string() ? values[f.name].get<std::string>() : "";
      s.resize(f.array_len, '\0');
      out.insert(out.end(), s.begin(), s.end());
      continue;
    }
    const std::size_t n = TypeSize(f.type);
    if (f.type == Type::kF32) {
      float x = values.contains(f.name) ? values[f.name].get<float>() : 0.0f;
      std::uint32_t u;
      std::memcpy(&u, &x, 4);
      PutInt(&out, u, 4, f.big_endian);
    } else if (f.type == Type::kF64) {
      double x = values.contains(f.name) ? values[f.name].get<double>() : 0.0;
      std::uint64_t u;
      std::memcpy(&u, &x, 8);
      PutInt(&out, u, 8, f.big_endian);
    } else {
      const std::int64_t x = values.contains(f.name) ? values[f.name].get<std::int64_t>() : 0;
      PutInt(&out, static_cast<std::uint64_t>(x), n, f.big_endian);
    }
  }
  return out;
}

json Unpack(const ModelMavlinkMessage& msg, const std::vector<std::uint8_t>& b) {
  json out = json::object();
  std::size_t off = 0;
  for (const auto& f : msg.fields) {
    if (f.role != Role::kPayload) {
      continue;
    }
    if (f.type == Type::kChar) {
      if (off + f.array_len > b.size()) return out;
      std::string s(reinterpret_cast<const char*>(b.data()) + off, f.array_len);
      s = s.substr(0, s.find('\0'));
      out[f.name] = s;
      off += f.array_len;
      continue;
    }
    const std::size_t n = TypeSize(f.type);
    if (off + n > b.size()) return out;
    const std::uint64_t u = GetInt(b, off, n, f.big_endian);
    switch (f.type) {
      case Type::kF32: {
        const std::uint32_t w = static_cast<std::uint32_t>(u);
        float x;
        std::memcpy(&x, &w, 4);
        out[f.name] = x;
        break;
      }
      case Type::kF64: { double x; std::memcpy(&x, &u, 8); out[f.name] = x; break; }
      case Type::kI8: out[f.name] = static_cast<std::int8_t>(u); break;
      case Type::kI16: out[f.name] = static_cast<std::int16_t>(u); break;
      case Type::kI32: out[f.name] = static_cast<std::int32_t>(u); break;
      case Type::kI64: out[f.name] = static_cast<std::int64_t>(u); break;
      default: out[f.name] = u; break;
    }
    off += n;
  }
  return out;
}

class Peer {
 public:
  void Start(const json& cfg) {
    ara::core::InitializeComDb(ara::core::ModelDataBase::GetInstance());
    ara::core::ModelDataBase::GetInstance().ForEach(
        [&](const ara::core::StringView&, const ara::core::ModelItem& item) {
          const auto m = item.GetValue<ModelMavlink>();
          if (m.has_value() && elements_.empty()) {
            elements_ = m->options_.elements;
            dialect_ = m->dialect_;
          }
        });
    if (elements_.empty()) {
      throw std::runtime_error("brak modelu MAVLink w wygenerowanym initialization.cc");
    }
    if (cfg.contains("method_timeout_ms")) {  // skrocenie timeoutu metod (domyslnie z modelu)
      for (auto& e : elements_) {
        if (!e.is_event) e.timeout_ms = cfg["method_timeout_ms"].get<std::uint32_t>();
      }
    }
    sysid_ = cfg.value("sysid", 1);
    compid_ = cfg.value("compid", 1);
    registry_ = std::make_unique<LinkTransportRegistry>(
        [](const std::string&) { return ara::core::Result<void>{}; });
    LinkManager::Options o;
    o.registry = registry_.get();
    if (cfg.contains("seq_start") && cfg["seq_start"].is_number()) {  // hak testowy (fix3): wymuszony poczatkowy seq
      o.test_initial_seq = cfg["seq_start"].get<std::uint16_t>();
    }
    manager_ = std::make_unique<LinkManager>(o);

    std::map<std::string, ModelMavlinkLink> links;
    for (const auto& l : cfg["links"]) {
      ModelMavlinkLink m;
      m.name = l["name"].get<std::string>();
      m.version = static_cast<std::uint8_t>(l.value("version", 2));
      m.heartbeat_hz = l.value("heartbeat_hz", 0.0);
      if (l.contains("device") && l["device"].is_string()) {
        // platforma nie otwiera urządzeń: peer rejestruje port jak aplikacja (RegisterLinkTransport)
        RegisterTransport(m.name, l["device"].get<std::string>());
      }
      if (l.contains("key_hex") && l["key_hex"].is_string()) {
        m.signing_enabled = true;
        m.signing_key_file = WriteKey(m.name, l["key_hex"].get<std::string>());
        m.signing_link_id = static_cast<std::uint32_t>(l.value("link_id", 0));
        m.accept_unsigned = l.value("accept_unsigned", false);
        if (l.contains("state_file") && l["state_file"].is_string()) {
          m.signing_state_file = l["state_file"].get<std::string>();
        }
      }
      links[m.name] = m;
    }
    for (const auto& ic : cfg["instances"]) {
      ModelMavlinkOptions opts;
      opts.elements = elements_;
      for (const auto& e : ic.value("exclude", json::array())) opts.exclude.push_back(e.get<std::string>());
      const json rates = ic.value("max_rate_hz", json::object());  // lokalna kopia: items() na tymczasowym wisi
      for (const auto& kv : rates.items()) opts.max_rate_hz[kv.key()] = kv.value().get<double>();
      opts.dedup_ttl_ms = ic.value("dedup_ttl_ms", 10000);
      std::vector<ModelMavlinkLink> ls;
      for (const auto& ln : ic["links"]) ls.push_back(links.at(ln.get<std::string>()));
      const std::uint16_t inst = ic["instance"].get<std::uint16_t>();
      const std::string base = "peer_i" + std::to_string(inst);
      const std::uint8_t ts = ic.value("target_system", 0), tc = ic.value("target_component", 0);
      auto sk = ModelMavlink::CreatSkeletonItem(base + "_sk", inst, 1, 0, sysid_, compid_, ts, tc, dialect_, ls, opts);
      auto px = ModelMavlink::CreatProxyItem(base + "_px", inst, 1, 0, sysid_, compid_, ts, tc, dialect_, ls, opts);
      auto esk = manager_->AttachEndpoint(sk.Value(), true);
      auto epx = manager_->AttachEndpoint(px.Value(), false);
      Wire(inst, esk, epx);
      skeletons_[inst] = esk;
      proxies_[inst] = epx;
      link_names_.insert(link_names_.end(), ic["links"].begin(), ic["links"].end());
    }
  }

  void Emit(const json& r) {
    const auto inst = r["instance"].get<std::uint16_t>();
    const auto* el = Find(r["element"].get<std::string>(), true);
    if (el == nullptr) throw std::runtime_error("nieznany event");
    const auto res = skeletons_.at(inst)->PublishEvent(el->id, Pack(el->req, r["values"]));
    (void)res;  // wykluczony = no-op + licznik (nie blad)
  }

  json Call(const json& r) {
    const auto inst = r["instance"].get<std::uint16_t>();
    const auto* el = Find(r["method"].get<std::string>(), false);
    if (el == nullptr) throw std::runtime_error("nieznana metoda");
    const auto res = proxies_.at(inst)->CallMethod(el->id, Pack(el->req, r["args"]));
    if (res.HasValue()) {
      return json::array({"ok", 0, Unpack(el->resp, res.Value())});
    }
    const int code = static_cast<int>(res.Error().Value());
    if (code == static_cast<int>(ara::com::ComErrc::kPeerIsUnreachable)) return json::array({"timeout"});
    return json::array({"error", code});
  }

  void RegisterTransport(const std::string& name, const std::string& device) {
    auto t = std::make_shared<ara::com::mavlink::SerialTransport>(name, device, 921600);
    const auto r = registry_->Register(name, t);
    if (!r.HasValue()) throw std::runtime_error("Register: " + std::string(r.Error().Message().data()));
  }

  json Stats() {
    json s = {{"rx_ok", 0}, {"rx_crc_err", 0}, {"rx_sig_err", 0}, {"rx_dropped", 0}, {"rx_duplicate", 0},
              {"rx_rejected", 0}, {"rx_unknown_instance", 0}, {"rx_target", 0}, {"tx", 0}, {"tx_excluded", 0},
              {"tx_dropped", 0}, {"tx_rate_held", 0}, {"req_executed", 0}, {"resp_ok", 0}, {"calls", 0},
              {"call_timeout", 0}, {"events_rx", 0}};
    std::set<std::string> seen;
    for (const auto& n : link_names_) {
      const auto name = n.get<std::string>();
      if (!seen.insert(name).second) continue;
      if (const auto l = manager_->FindLink(name)) {
        const auto c = l->counters();
        s["rx_ok"] = s["rx_ok"].get<std::uint64_t>() + c.rx_ok;
        s["rx_crc_err"] = s["rx_crc_err"].get<std::uint64_t>() + c.rx_crc_err;
        s["rx_sig_err"] = s["rx_sig_err"].get<std::uint64_t>() + c.rx_sig_err;
        s["rx_dropped"] = s["rx_dropped"].get<std::uint64_t>() + c.rx_dropped;
        s["tx"] = s["tx"].get<std::uint64_t>() + c.tx;
        s["tx_dropped"] = s["tx_dropped"].get<std::uint64_t>() + c.tx_dropped;
      }
    }
    s["rx_duplicate"] = manager_->duplicates();
    for (const auto& kv : skeletons_) {
      const auto c = kv.second->counters();
      s["req_executed"] = s["req_executed"].get<std::uint64_t>() + c.rx_requests;
      s["rx_rejected"] = s["rx_rejected"].get<std::uint64_t>() + c.rx_rejected;
      s["tx_excluded"] = s["tx_excluded"].get<std::uint64_t>() + c.tx_excluded;
    }
    for (const auto& kv : proxies_) {
      const auto c = kv.second->counters();
      s["resp_ok"] = s["resp_ok"].get<std::uint64_t>() + c.rx_responses;
      s["calls"] = s["calls"].get<std::uint64_t>() + c.tx_requests;
      s["call_timeout"] = s["call_timeout"].get<std::uint64_t>() + c.call_timeouts;
      s["events_rx"] = s["events_rx"].get<std::uint64_t>() + c.rx_events;
    }
    return s;
  }

  void Stop() {
    if (manager_) manager_->Shutdown();
  }

 private:
  const ModelMavlinkElement* Find(const std::string& name, bool event) const {
    for (const auto& e : elements_) {
      if (e.name == name && e.is_event == event) return &e;
    }
    return nullptr;
  }
  const ModelMavlinkElement* ById(std::uint16_t id) const {
    for (const auto& e : elements_) {
      if (e.id == id) return &e;
    }
    return nullptr;
  }

  void Wire(std::uint16_t inst, const std::shared_ptr<MavlinkEndpoint>& sk,
            const std::shared_ptr<MavlinkEndpoint>& px) {
    sk->SetMethodHandler([this, inst](std::uint16_t id, const std::vector<std::uint8_t>& payload)
                             -> ara::core::Result<std::vector<std::uint8_t>> {
      const auto* el = ById(id);
      Send({{"ev", "handled"}, {"handled", json::array({inst, el ? el->name : "?", 0, 0})}});
      return payload;  // SetMode: echo (RESP.mode = REQ.mode); ten sam uklad pola `mode`
    });
    px->SetEventHandler([this, inst](std::uint16_t id, const std::vector<std::uint8_t>& payload) {
      const auto* el = ById(id);
      if (el == nullptr) return;
      json ev = {{"type", el->req.name}, {"instance", inst}, {"seq", 0}, {"link", ""},
                 {"values", Unpack(el->req, payload)}};
      Send({{"ev", "event"}, {"event", ev}});
    });
  }

  std::string WriteKey(const std::string& link, const std::string& hex) {
    const char* dir = std::getenv("TEST_TMPDIR");
    const std::string path = std::string(dir ? dir : "/tmp") + "/peer_key_" + std::to_string(getpid_()) + "_" + link;
    std::ofstream f(path);
    f << hex;
    return path;
  }
  static int getpid_() { return static_cast<int>(::getpid()); }

  std::vector<ModelMavlinkElement> elements_;
  std::string dialect_;
  std::uint8_t sysid_{1}, compid_{1};
  std::unique_ptr<LinkTransportRegistry> registry_;
  std::unique_ptr<LinkManager> manager_;
  std::map<std::uint16_t, std::shared_ptr<MavlinkEndpoint>> skeletons_, proxies_;
  std::vector<json> link_names_;
};

long RssKb() {  // NOLINT(runtime/int)
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("VmRSS:", 0) == 0) return std::stol(line.substr(6));
  }
  return 0;
}
}  // namespace

int main() {
  Peer peer;
  std::string line;
  while (std::getline(std::cin, line)) {
    json req;
    try {
      req = json::parse(line);
    } catch (...) {
      continue;
    }
    json res = {{"id", req["id"]}, {"ok", true}, {"result", nullptr}};
    try {
      const std::string cmd = req["cmd"];
      if (cmd == "start") {
        peer.Start(req["config"]);
      } else if (cmd == "emit") {
        peer.Emit(req);
      } else if (cmd == "call") {
        res["result"] = peer.Call(req);
      } else if (cmd == "register_transport") {
        peer.RegisterTransport(req["name"], req["device"]);
      } else if (cmd == "stats") {
        res["result"] = peer.Stats();
      } else if (cmd == "rss") {
        res["result"] = RssKb();
      } else if (cmd == "stop") {
        peer.Stop();
        Send(res);
        return 0;
      }
    } catch (const std::exception& e) {
      res = {{"id", req["id"]}, {"ok", false}, {"error", e.what()}};
    }
    Send(res);
  }
  peer.Stop();
  return 0;
}
