/**
 * Copyright (c) 2024
 *
 * @file mavlink_interop_tool.cc
 * @brief Narzędzie testowe interopu C++ <-> pymavlink. Opisy wiadomości pochodzą z
 * wygenerowanego initialization.cc (model testowej aplikacji), czyli z tego samego
 * źródła co dialekt XML używany przez pymavlink.
 *
 *   emit   <ts>     -> linie `FRAME <nazwa> <v1|v2|v2s> <hex ramki> SOMEIP <hex>`
 *   decode <plik>   -> dla ramek z pliku (`FRAME <nazwa> <fmt> <hex>`) linie
 *                      `OUT <nazwa> <fmt> seq=.. instance=.. tsys=.. tcomp=.. status=..
 *                      someip=<hex> sig=<ok|bad|none>`
 */
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ara/com/mavlink/mavlink_codec.h"
#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/com/mavlink/mavlink_signing.h"
#include "ara/core/instance_specifier.h"
#include "ara/core/model_db.h"

namespace ara {
namespace core {
Result<void> InitializeComDb(ModelDataBase& db_) noexcept;  // NOLINT(runtime/references) generowane
}  // namespace core
}  // namespace ara

namespace {
using ara::com::mavlink::MessageCodec;
using ara::com::mavlink::MessageHeader;

std::string Hex(const std::vector<std::uint8_t>& v) {
  static const char* d = "0123456789abcdef";
  std::string o;
  for (const auto b : v) {
    o += d[b >> 4];
    o += d[b & 15];
  }
  return o;
}

std::vector<std::uint8_t> FromHex(const std::string& s) {
  std::vector<std::uint8_t> v;
  for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
    v.push_back(static_cast<std::uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
  }
  return v;
}

ara::com::mavlink::SigningKey TestKey() {
  ara::com::mavlink::SigningKey k{};
  for (std::size_t i = 0; i < k.size(); ++i) {
    k[i] = static_cast<std::uint8_t>(i + 1);
  }
  return k;
}

/// Deterministyczne bajty SOME/IP (bez NaN/Inf: bajty < 64), tekst dla surowego stringa.
std::vector<std::uint8_t> Sample(std::size_t len, bool raw_string) {
  std::vector<std::uint8_t> v(len);
  if (raw_string) {
    const std::string text = "hello";
    v.assign(text.begin(), text.end());
    return v;
  }
  for (std::size_t i = 0; i < len; ++i) {
    v[i] = static_cast<std::uint8_t>((i * 3 + 1) % 64);
  }
  return v;
}

struct Entry {
  std::string name;
  std::shared_ptr<MessageCodec> codec;
  std::size_t someip_len;
  bool raw_string;
};

std::vector<Entry> LoadEntries() {
  ara::core::InitializeComDb(ara::core::ModelDataBase::GetInstance());
  const auto model = ara::core::ModelDataBase::GetInstance()
                         .ResolveInstanceSpecifier<ara::core::model::ModelMavlink>(
                             ara::core::InstanceSpecifier{"srp/mav/MavApp/radio_out"});
  std::vector<Entry> out;
  if (!model.HasValue()) {
    std::cerr << "brak modelu radio_out\n";
    return out;
  }
  for (const auto& el : model.Value().options_.elements) {
    if (!el.enabled) {
      continue;
    }
    for (const auto* msg : {&el.req, el.has_resp ? &el.resp : nullptr}) {
      if (msg == nullptr) {
        continue;
      }
      Entry e;
      e.name = msg->name;
      e.codec = std::make_shared<MessageCodec>(*msg);
      e.someip_len = e.codec->someip_len();
      std::size_t payload_fields = 0;
      bool has_char = false;
      for (const auto& f : msg->fields) {
        if (f.role == ara::core::model::ModelMavlinkField::Role::kPayload) {
          ++payload_fields;
          has_char = has_char || f.type == ara::core::model::ModelMavlinkField::Type::kChar;
        }
      }
      e.raw_string = payload_fields == 1 && has_char;
      out.push_back(e);
    }
  }
  return out;
}

int Emit(std::uint64_t ts) {
  const auto entries = LoadEntries();
  if (entries.empty()) {
    return 2;
  }
  const auto key = TestKey();
  for (const auto& e : entries) {
    const auto someip = Sample(e.someip_len, e.raw_string);
    MessageHeader h;
    h.seq = 1234;
    h.instance = 3;
    h.target_system = 2;
    h.target_component = 1;
    h.status = 0;
    const auto payload = e.codec->Encode(h, someip);
    if (!payload.HasValue()) {
      std::cerr << "encode " << e.name << ": " << payload.Error().SupportData() << "\n";
      return 3;
    }
    const ara::com::mavlink::SigningParams sp{key, 7, ts};
    const struct {
      const char* fmt;
      int version;
      const ara::com::mavlink::SigningParams* sign;
    } variants[] = {{"v1", 1, nullptr}, {"v2", 2, nullptr}, {"v2s", 2, &sp}};
    for (const auto& v : variants) {
      const auto frame = ara::com::mavlink::BuildFrame(
          static_cast<std::uint8_t>(v.version), 9, 1, 1, e.codec->msg_id(), payload.Value(),
          e.codec->crc_extra(), v.sign);
      std::cout << "FRAME " << e.name << " " << v.fmt << " " << Hex(frame) << " SOMEIP "
                << Hex(someip) << "\n";
    }
  }
  return 0;
}

int Decode(const std::string& path) {
  const auto entries = LoadEntries();
  if (entries.empty()) {
    return 2;
  }
  std::map<std::string, Entry> by_name;
  std::map<std::uint32_t, Entry> by_id;
  for (const auto& e : entries) {
    by_name[e.name] = e;
    by_id[e.codec->msg_id()] = e;
  }
  ara::com::mavlink::LinkSigner::Config sc;
  sc.key = TestKey();
  sc.link_id = 7;
  sc.accept_unsigned = true;
  ara::com::mavlink::LinkSigner signer{sc, [] { return std::uint64_t{0}; }};
  std::ifstream in{path};
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ss{line};
    std::string tag, name, fmt, hex;
    ss >> tag >> name >> fmt >> hex;
    if (tag != "FRAME") {
      continue;
    }
    const auto bytes = FromHex(hex);
    const auto it = by_name.find(name);
    if (it == by_name.end()) {
      std::cout << "OUT " << name << " " << fmt << " unknown\n";
      continue;
    }
    ara::com::mavlink::FrameParser parser{
        [&by_id](std::uint32_t id) -> std::optional<std::uint8_t> {
          const auto found = by_id.find(id);
          if (found == by_id.end()) {
            return std::nullopt;
          }
          return found->second.codec->crc_extra();
        }};
    std::vector<ara::com::mavlink::Frame> frames;
    parser.Feed(bytes, &frames);
    if (frames.size() != 1) {
      std::cout << "OUT " << name << " " << fmt << " parse_error frames=" << frames.size() << "\n";
      continue;
    }
    const auto dec = it->second.codec->Decode(frames[0].payload);
    if (!dec.HasValue()) {
      std::cout << "OUT " << name << " " << fmt << " decode_error\n";
      continue;
    }
    const char* sig = "none";
    if (frames[0].has_signature) {
      sig = signer.Verify(frames[0]) == ara::com::mavlink::SignatureVerdict::kOk ? "ok" : "bad";
    }
    const auto& h = dec.Value().header;
    std::cout << "OUT " << name << " " << fmt << " seq=" << h.seq << " instance=" << h.instance
              << " tsys=" << static_cast<int>(h.target_system)
              << " tcomp=" << static_cast<int>(h.target_component)
              << " status=" << static_cast<int>(h.status) << " frame_seq="
              << static_cast<int>(frames[0].seq) << " someip=" << Hex(dec.Value().someip)
              << " sig=" << sig << "\n";
  }
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::string(argv[1]) == "emit") {
    return Emit(std::stoull(argv[2]));
  }
  if (argc >= 3 && std::string(argv[1]) == "decode") {
    return Decode(argv[2]);
  }
  std::cerr << "uzycie: mavlink_interop_tool emit <ts> | decode <plik>\n";
  return 1;
}
