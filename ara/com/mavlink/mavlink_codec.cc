/**
 * Copyright (c) 2024
 *
 * @file mavlink_codec.cc
 */
#include "ara/com/mavlink/mavlink_codec.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "ara/com/com_error_domain.h"
#include "ara/com/mavlink/mavlink_frame.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using Field = ara::core::model::ModelMavlinkField;

std::size_t ElemSize(Field::Type t) {
  switch (t) {
    case Field::Type::kU8:
    case Field::Type::kI8:
    case Field::Type::kChar:
      return 1;
    case Field::Type::kU16:
    case Field::Type::kI16:
      return 2;
    case Field::Type::kU32:
    case Field::Type::kI32:
    case Field::Type::kF32:
      return 4;
    case Field::Type::kU64:
    case Field::Type::kI64:
    case Field::Type::kF64:
      return 8;
  }
  return 1;
}

const char* TypeName(Field::Type t) {
  switch (t) {
    case Field::Type::kU8: return "uint8_t";
    case Field::Type::kI8: return "int8_t";
    case Field::Type::kU16: return "uint16_t";
    case Field::Type::kI16: return "int16_t";
    case Field::Type::kU32: return "uint32_t";
    case Field::Type::kI32: return "int32_t";
    case Field::Type::kU64: return "uint64_t";
    case Field::Type::kI64: return "int64_t";
    case Field::Type::kF32: return "float";
    case Field::Type::kF64: return "double";
    case Field::Type::kChar: return "char";
  }
  return "uint8_t";
}

ara::core::ErrorCode Invalid(const std::string& what) {
  return ara::com::MakeErrorCode(ara::com::ComErrc::kFieldValueIsNotValid, what);
}

void PutLe(std::vector<std::uint8_t>* buf, std::size_t off, std::uint64_t v, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    (*buf)[off + i] = static_cast<std::uint8_t>(v >> (8 * i));
  }
}

std::uint64_t GetLe(const std::vector<std::uint8_t>& buf, std::size_t off, std::size_t n) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < n; ++i) {
    v |= static_cast<std::uint64_t>(buf[off + i]) << (8 * i);
  }
  return v;
}

}  // namespace

MessageCodec::MessageCodec(const ara::core::model::ModelMavlinkMessage& msg)
    : msg_id_{msg.msg_id}, crc_extra_{msg.crc_extra}, name_{msg.name} {
  slots_.reserve(msg.fields.size());
  std::size_t payload_fields = 0;
  for (const auto& f : msg.fields) {
    Slot s;
    s.field = f;
    s.elem_size = ElemSize(f.type);
    s.total_size = s.elem_size * std::max<std::size_t>(1, f.array_len);
    slots_.push_back(s);
    if (f.role == Field::Role::kPayload) {
      ++payload_fields;
    }
  }
  // Reprezentacja SOME/IP: pola payloadu w kolejności modelu.
  std::size_t some_off = 0;
  for (auto& s : slots_) {
    if (s.field.role == Field::Role::kPayload) {
      s.someip_offset = some_off;
      some_off += s.total_size;
    }
  }
  someip_len_ = some_off;
  raw_string_ = payload_fields == 1 &&
                std::any_of(slots_.begin(), slots_.end(), [](const Slot& s) {
                  return s.field.role == Field::Role::kPayload &&
                         s.field.type == Field::Type::kChar;
                });
  // Układ na przewodzie: malejący rozmiar elementu, stabilnie.
  std::vector<std::size_t> order(slots_.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
    return slots_[a].elem_size > slots_[b].elem_size;
  });
  std::size_t wire_off = 0;
  for (const auto idx : order) {
    slots_[idx].wire_offset = wire_off;
    wire_off += slots_[idx].total_size;
  }
  full_len_ = wire_off;
}

std::optional<std::size_t> MessageCodec::HeaderOffset(Field::Role role) const {
  for (const auto& s : slots_) {
    if (s.field.role == role) {
      return s.wire_offset;
    }
  }
  return std::nullopt;
}

ara::core::Result<std::vector<std::uint8_t>> MessageCodec::Encode(
    const MessageHeader& h, const std::vector<std::uint8_t>& someip) const {
  if (full_len_ > kMaxPayload) {
    return Invalid(name_ + ": payload > 255 B");
  }
  if (raw_string_) {
    const Slot* str = nullptr;
    for (const auto& s : slots_) {
      if (s.field.role == Field::Role::kPayload) {
        str = &s;
      }
    }
    if (someip.size() > str->total_size) {
      return Invalid(name_ + ": string dłuższy niż max_len=" +
                     std::to_string(str->total_size));
    }
  } else if (someip.size() < someip_len_) {
    return Invalid(name_ + ": payload SOME/IP ma " + std::to_string(someip.size()) +
                   " B, oczekiwano " + std::to_string(someip_len_));
  }
  std::vector<std::uint8_t> out(full_len_, 0);
  for (const auto& s : slots_) {
    switch (s.field.role) {
      case Field::Role::kSeq: PutLe(&out, s.wire_offset, h.seq, 2); break;
      case Field::Role::kInstance: PutLe(&out, s.wire_offset, h.instance, 2); break;
      case Field::Role::kTargetSystem: out[s.wire_offset] = h.target_system; break;
      case Field::Role::kTargetComponent: out[s.wire_offset] = h.target_component; break;
      case Field::Role::kStatus: out[s.wire_offset] = h.status; break;
      case Field::Role::kPayload: {
        if (s.field.type == Field::Type::kChar) {
          const std::size_t n = std::min(s.total_size, someip.size() - s.someip_offset);
          std::memcpy(out.data() + s.wire_offset, someip.data() + s.someip_offset, n);
        } else if (s.field.big_endian && s.elem_size > 1) {
          for (std::size_t i = 0; i < s.elem_size; ++i) {
            out[s.wire_offset + i] = someip[s.someip_offset + s.elem_size - 1 - i];
          }
        } else {
          std::memcpy(out.data() + s.wire_offset, someip.data() + s.someip_offset, s.elem_size);
        }
        break;
      }
    }
  }
  return out;
}

ara::core::Result<DecodedMessage> MessageCodec::Decode(
    const std::vector<std::uint8_t>& payload) const {
  if (payload.size() > full_len_) {
    return Invalid(name_ + ": payload " + std::to_string(payload.size()) +
                   " B dłuższy niż definicja (" + std::to_string(full_len_) + " B)");
  }
  const auto full = ExpandPayload(payload, full_len_);
  DecodedMessage d;
  d.someip.assign(someip_len_, 0);
  for (const auto& s : slots_) {
    switch (s.field.role) {
      case Field::Role::kSeq: d.header.seq = static_cast<std::uint16_t>(GetLe(full, s.wire_offset, 2)); break;
      case Field::Role::kInstance: d.header.instance = static_cast<std::uint16_t>(GetLe(full, s.wire_offset, 2)); break;
      case Field::Role::kTargetSystem: d.header.target_system = full[s.wire_offset]; break;
      case Field::Role::kTargetComponent: d.header.target_component = full[s.wire_offset]; break;
      case Field::Role::kStatus: d.header.status = full[s.wire_offset]; break;
      case Field::Role::kPayload: {
        if (s.field.type == Field::Type::kChar) {
          std::memcpy(d.someip.data() + s.someip_offset, full.data() + s.wire_offset, s.total_size);
        } else if (s.field.big_endian && s.elem_size > 1) {
          for (std::size_t i = 0; i < s.elem_size; ++i) {
            d.someip[s.someip_offset + i] = full[s.wire_offset + s.elem_size - 1 - i];
          }
        } else {
          std::memcpy(d.someip.data() + s.someip_offset, full.data() + s.wire_offset, s.elem_size);
        }
        break;
      }
    }
  }
  if (raw_string_) {
    // Dekoder ucina tekst na pierwszym NUL (char[N] bez NUL jest dozwolony).
    const auto nul = std::find(d.someip.begin(), d.someip.end(), 0);
    d.someip.erase(nul, d.someip.end());
  }
  return d;
}

std::uint8_t ComputeCrcExtra(const ara::core::model::ModelMavlinkMessage& msg) {
  std::vector<const Field*> ordered;
  for (const auto& f : msg.fields) {
    ordered.push_back(&f);
  }
  std::stable_sort(ordered.begin(), ordered.end(), [](const Field* a, const Field* b) {
    return ElemSize(a->type) > ElemSize(b->type);
  });
  std::uint16_t crc = 0xFFFF;
  auto feed = [&crc](const std::string& s) {
    for (const char c : s) {
      crc = CrcAccumulate(crc, static_cast<std::uint8_t>(c));
    }
  };
  feed(msg.name + " ");
  for (const auto* f : ordered) {
    feed(std::string(TypeName(f->type)) + " ");
    feed(f->name + " ");
    if (f->array_len > 0) {
      crc = CrcAccumulate(crc, static_cast<std::uint8_t>(f->array_len));
    }
  }
  return static_cast<std::uint8_t>((crc & 0xFF) ^ (crc >> 8));
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
