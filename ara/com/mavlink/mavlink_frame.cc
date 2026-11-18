/**
 * Copyright (c) 2024
 *
 * @file mavlink_frame.cc
 */
#include "ara/com/mavlink/mavlink_frame.h"

#include <algorithm>
#include <utility>

#include "ara/com/mavlink/sha256.h"

namespace ara {
namespace com {
namespace mavlink {

std::uint16_t CrcAccumulate(std::uint16_t crc, std::uint8_t byte) {
  std::uint8_t tmp = static_cast<std::uint8_t>(byte ^ static_cast<std::uint8_t>(crc & 0xFF));
  tmp = static_cast<std::uint8_t>(tmp ^ static_cast<std::uint8_t>(tmp << 4));
  return static_cast<std::uint16_t>((crc >> 8) ^ (static_cast<std::uint16_t>(tmp) << 8) ^
                                    (static_cast<std::uint16_t>(tmp) << 3) ^
                                    (static_cast<std::uint16_t>(tmp) >> 4));
}

std::uint16_t CrcCalculate(std::span<const std::uint8_t> data) {
  std::uint16_t crc = 0xFFFF;
  for (const auto b : data) {
    crc = CrcAccumulate(crc, b);
  }
  return crc;
}

std::array<std::uint8_t, 6> ComputeSignature(
    std::span<const std::uint8_t> key, std::span<const std::uint8_t> frame_wo_signature,
    std::uint8_t link_id, std::uint64_t timestamp) {
  Sha256 sha;
  sha.Update(key);
  sha.Update(frame_wo_signature);
  const std::uint8_t tail[7] = {
      link_id,
      static_cast<std::uint8_t>(timestamp),
      static_cast<std::uint8_t>(timestamp >> 8),
      static_cast<std::uint8_t>(timestamp >> 16),
      static_cast<std::uint8_t>(timestamp >> 24),
      static_cast<std::uint8_t>(timestamp >> 32),
      static_cast<std::uint8_t>(timestamp >> 40)};
  sha.Update(tail);
  const auto d = sha.Finalize();
  std::array<std::uint8_t, 6> sig{};
  std::copy_n(d.begin(), 6, sig.begin());
  return sig;
}

std::vector<std::uint8_t> BuildFrame(std::uint8_t version, std::uint8_t seq,
                                     std::uint8_t sysid, std::uint8_t compid,
                                     std::uint32_t msgid,
                                     std::span<const std::uint8_t> payload,
                                     std::uint8_t crc_extra,
                                     const SigningParams* signing) {
  std::size_t len = std::min<std::size_t>(payload.size(), kMaxPayload);
  std::vector<std::uint8_t> f;
  if (version == 1) {
    f = {kMagicV1, static_cast<std::uint8_t>(len), seq, sysid, compid,
         static_cast<std::uint8_t>(msgid)};
  } else {
    while (len > 1 && payload[len - 1] == 0) {
      --len;  // obcięcie końcowych zer (zostaje >= 1 bajt)
    }
    const bool sign = signing != nullptr;
    f = {kMagicV2, static_cast<std::uint8_t>(len),
         static_cast<std::uint8_t>(sign ? kIncompatSigned : 0), 0, seq, sysid,
         compid, static_cast<std::uint8_t>(msgid),
         static_cast<std::uint8_t>(msgid >> 8),
         static_cast<std::uint8_t>(msgid >> 16)};
  }
  f.insert(f.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(len));
  std::uint16_t crc = CrcCalculate(std::span<const std::uint8_t>{f.data() + 1, f.size() - 1});
  crc = CrcAccumulate(crc, crc_extra);
  f.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  f.push_back(static_cast<std::uint8_t>(crc >> 8));
  if (version != 1 && signing != nullptr) {
    const auto sig = ComputeSignature(signing->key, f, signing->link_id, signing->timestamp);
    f.push_back(signing->link_id);
    for (int i = 0; i < 6; ++i) {
      f.push_back(static_cast<std::uint8_t>(signing->timestamp >> (8 * i)));
    }
    f.insert(f.end(), sig.begin(), sig.end());
  }
  return f;
}

std::vector<std::uint8_t> ExpandPayload(const std::vector<std::uint8_t>& payload,
                                        std::size_t full_len) {
  std::vector<std::uint8_t> out = payload;
  if (out.size() < full_len) {
    out.resize(full_len, 0);
  }
  return out;
}

std::size_t FrameParser::ValidFrameLengthAt(std::size_t pos) const {
  const std::uint8_t magic = buf_[pos];
  if (magic != kMagicV1 && magic != kMagicV2) {
    return 0;
  }
  const bool v2 = magic == kMagicV2;
  const std::size_t hdr = v2 ? 10 : 6;
  if (buf_.size() - pos < hdr) {
    return 0;
  }
  const std::size_t len = buf_[pos + 1];
  const std::uint8_t incompat = v2 ? buf_[pos + 2] : 0;
  if (v2 && (incompat & ~kIncompatSigned) != 0) {
    return 0;
  }
  const bool signed_frame = v2 && (incompat & kIncompatSigned) != 0;
  const std::size_t total = hdr + len + 2 + (signed_frame ? kSignatureLen : 0);
  if (buf_.size() - pos < total) {
    return 0;
  }
  const std::uint32_t msgid =
      v2 ? (static_cast<std::uint32_t>(buf_[pos + 7]) |
            (static_cast<std::uint32_t>(buf_[pos + 8]) << 8) |
            (static_cast<std::uint32_t>(buf_[pos + 9]) << 16))
         : buf_[pos + 5];
  const auto extra = lookup_ ? lookup_(msgid) : std::nullopt;
  if (!extra.has_value()) {
    return 0;
  }
  std::uint16_t crc = CrcCalculate(std::span<const std::uint8_t>{&buf_[pos + 1], hdr - 1 + len});
  crc = CrcAccumulate(crc, *extra);
  const std::uint16_t got = static_cast<std::uint16_t>(buf_[pos + hdr + len]) |
                            (static_cast<std::uint16_t>(buf_[pos + hdr + len + 1]) << 8);
  return crc == got ? total : 0;
}

void FrameParser::Feed(std::span<const std::uint8_t> data, std::vector<Frame>* out) {
  buf_.insert(buf_.end(), data.begin(), data.end());
  std::size_t pos = 0;
  while (pos < buf_.size()) {
    const std::uint8_t magic = buf_[pos];
    if (magic != kMagicV1 && magic != kMagicV2) {
      ++stats_.garbage_bytes;
      ++pos;
      continue;
    }
    const bool v2 = magic == kMagicV2;
    const std::size_t hdr = v2 ? 10 : 6;
    if (buf_.size() - pos < hdr) {
      break;  // czekamy na resztę nagłówka
    }
    const std::size_t len = buf_[pos + 1];
    const std::uint8_t incompat = v2 ? buf_[pos + 2] : 0;
    if (v2 && (incompat & ~kIncompatSigned) != 0) {
      ++stats_.bad_incompat_flags;  // nieznana flaga: ramkę odrzucamy
      ++pos;
      continue;
    }
    const bool signed_frame = v2 && (incompat & kIncompatSigned) != 0;
    const std::size_t total = hdr + len + 2 + (signed_frame ? kSignatureLen : 0);
    if (buf_.size() - pos < total) {
      // Niekompletna ramka. Jeśli w już odebranych bajtach siedzi kompletna,
      // poprawna ramka, to ten "start" jest fałszywy (np. śmieć z dużym len) i
      // nie wolno na niego czekać — inaczej zablokowałby kolejne ramki.
      bool later_valid = false;
      for (std::size_t q = pos + 1; q < buf_.size() && !later_valid; ++q) {
        later_valid = ValidFrameLengthAt(q) != 0;
      }
      if (later_valid) {
        ++pos;
        continue;
      }
      break;
    }
    const std::uint32_t msgid =
        v2 ? (static_cast<std::uint32_t>(buf_[pos + 7]) |
              (static_cast<std::uint32_t>(buf_[pos + 8]) << 8) |
              (static_cast<std::uint32_t>(buf_[pos + 9]) << 16))
           : buf_[pos + 5];
    const auto extra = lookup_ ? lookup_(msgid) : std::nullopt;
    if (!extra.has_value()) {
      ++stats_.unknown_msgid;
      ++pos;  // nie da się zweryfikować CRC: szukamy dalej (fałszywy start nie połyka ramek)
      continue;
    }
    std::uint16_t crc = CrcCalculate(std::span<const std::uint8_t>{&buf_[pos + 1], hdr - 1 + len});
    crc = CrcAccumulate(crc, *extra);
    const std::uint16_t got = static_cast<std::uint16_t>(buf_[pos + hdr + len]) |
                              (static_cast<std::uint16_t>(buf_[pos + hdr + len + 1]) << 8);
    if (crc != got) {
      ++stats_.crc_errors;
      ++pos;  // fałszywy start: resynchronizacja od następnego bajtu
      continue;
    }
    Frame fr;
    fr.version = v2 ? 2 : 1;
    fr.incompat_flags = incompat;
    fr.compat_flags = v2 ? buf_[pos + 3] : 0;
    fr.seq = buf_[pos + (v2 ? 4 : 2)];
    fr.sysid = buf_[pos + (v2 ? 5 : 3)];
    fr.compid = buf_[pos + (v2 ? 6 : 4)];
    fr.msgid = msgid;
    fr.payload.assign(buf_.begin() + static_cast<std::ptrdiff_t>(pos + hdr),
                      buf_.begin() + static_cast<std::ptrdiff_t>(pos + hdr + len));
    fr.crc = got;
    if (signed_frame) {
      const std::size_t s = pos + hdr + len + 2;
      fr.has_signature = true;
      fr.link_id = buf_[s];
      fr.timestamp = 0;
      for (int i = 0; i < 6; ++i) {
        fr.timestamp |= static_cast<std::uint64_t>(buf_[s + 1 + i]) << (8 * i);
      }
      std::copy_n(buf_.begin() + static_cast<std::ptrdiff_t>(s + 7), 6, fr.signature.begin());
    }
    fr.raw.assign(buf_.begin() + static_cast<std::ptrdiff_t>(pos),
                  buf_.begin() + static_cast<std::ptrdiff_t>(pos + total));
    if (filter_ && !filter_(fr)) {
      ++stats_.filter_rejected;
      ++pos;  // jak przy złym CRC: rescan od bajtu po STX (nie połykamy następnej ramki)
      continue;
    }
    ++stats_.frames;
    if (out != nullptr) {
      out->push_back(std::move(fr));
    }
    pos += total;
  }
  buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos));
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
