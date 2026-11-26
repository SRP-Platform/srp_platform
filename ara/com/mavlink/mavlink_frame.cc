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

std::vector<std::uint8_t> BuildFrame(std::uint8_t seq, std::uint8_t sysid, std::uint8_t compid,
                                     std::uint32_t msgid,
                                     std::span<const std::uint8_t> payload,
                                     std::uint8_t crc_extra,
                                     const SigningParams* signing) {
  std::size_t len = std::min<std::size_t>(payload.size(), kMaxPayload);
  while (len > 1 && payload[len - 1] == 0) {
    --len;  // truncate trailing zeros (at least 1 byte stays)
  }
  std::vector<std::uint8_t> f = {kMagicV2, static_cast<std::uint8_t>(len),
                                 static_cast<std::uint8_t>(signing != nullptr ? kIncompatSigned : 0),
                                 0, seq, sysid, compid, static_cast<std::uint8_t>(msgid),
                                 static_cast<std::uint8_t>(msgid >> 8),
                                 static_cast<std::uint8_t>(msgid >> 16)};
  f.insert(f.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(len));
  std::uint16_t crc = CrcCalculate(std::span<const std::uint8_t>{f.data() + 1, f.size() - 1});
  crc = CrcAccumulate(crc, crc_extra);
  f.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  f.push_back(static_cast<std::uint8_t>(crc >> 8));
  if (signing != nullptr) {
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

FrameParser::Probed FrameParser::ProbeAt(std::size_t pos) const {
  Probed p;
  const std::uint8_t magic = buf_[pos];
  if (magic != kMagicV1 && magic != kMagicV2) {
    return p;
  }
  p.is_v1 = magic == kMagicV1;
  p.hdr = p.is_v1 ? 6 : 10;
  if (buf_.size() - pos < p.hdr) {
    p.verdict = Probe::kNeedHeader;
    return p;
  }
  p.len = buf_[pos + 1];
  const std::uint8_t incompat = p.is_v1 ? 0 : buf_[pos + 2];
  if (!p.is_v1 && (incompat & ~kIncompatSigned) != 0) {
    p.verdict = Probe::kBadFlags;
    return p;
  }
  p.is_signed = !p.is_v1 && (incompat & kIncompatSigned) != 0;
  p.total = p.hdr + p.len + 2 + (p.is_signed ? kSignatureLen : 0);
  if (buf_.size() - pos < p.total) {
    p.verdict = Probe::kIncomplete;
    return p;
  }
  p.msgid = p.is_v1 ? buf_[pos + 5]
                    : (static_cast<std::uint32_t>(buf_[pos + 7]) |
                       (static_cast<std::uint32_t>(buf_[pos + 8]) << 8) |
                       (static_cast<std::uint32_t>(buf_[pos + 9]) << 16));
  const auto extra = lookup_ ? lookup_(p.msgid) : std::nullopt;
  if (!extra.has_value()) {
    p.verdict = Probe::kUnknownMsgId;
    return p;
  }
  std::uint16_t crc = CrcCalculate(std::span<const std::uint8_t>{&buf_[pos + 1], p.hdr - 1 + p.len});
  crc = CrcAccumulate(crc, *extra);
  const std::uint16_t got = static_cast<std::uint16_t>(buf_[pos + p.hdr + p.len]) |
                            (static_cast<std::uint16_t>(buf_[pos + p.hdr + p.len + 1]) << 8);
  p.verdict = crc == got ? Probe::kValid : Probe::kBadCrc;
  return p;
}

void FrameParser::Feed(std::span<const std::uint8_t> data, std::vector<Frame>* out) {
  buf_.insert(buf_.end(), data.begin(), data.end());
  std::size_t pos = 0;
  while (pos < buf_.size()) {
    const Probed p = ProbeAt(pos);
    if (p.verdict == Probe::kNotStart) {
      ++stats_.garbage_bytes;
      ++pos;
      continue;
    }
    if (p.verdict == Probe::kNeedHeader) {
      break;  // wait for the rest of the header
    }
    if (p.verdict == Probe::kBadFlags) {
      ++stats_.bad_incompat_flags;  // unknown flag: drop the frame
      ++pos;
      continue;
    }
    if (p.verdict == Probe::kIncomplete) {
      // Incomplete frame. If the bytes already received contain a complete valid frame, this
      // "start" is bogus (e.g. garbage with a large len) and must not be waited for — it
      // would block the following frames.
      bool later_valid = false;
      for (std::size_t q = pos + 1; q < buf_.size() && !later_valid; ++q) {
        later_valid = ProbeAt(q).verdict == Probe::kValid;
      }
      if (later_valid) {
        ++pos;
        continue;
      }
      break;
    }
    if (p.verdict == Probe::kUnknownMsgId) {
      ++stats_.unknown_msgid;
      ++pos;  // CRC cannot be verified: keep scanning (a bogus start must not swallow frames)
      continue;
    }
    if (p.verdict == Probe::kBadCrc) {
      ++stats_.crc_errors;
      ++pos;  // bogus start: resynchronise at the next byte
      continue;
    }
    if (p.is_v1) {
      ++stats_.v1_rejected;  // a well-formed v1 frame: MAVLink v1 is unsupported, drop it whole
      pos += p.total;
      continue;
    }
    Frame fr;
    fr.incompat_flags = buf_[pos + 2];
    fr.seq = buf_[pos + 4];
    fr.sysid = buf_[pos + 5];
    fr.compid = buf_[pos + 6];
    fr.msgid = p.msgid;
    fr.payload.assign(buf_.begin() + static_cast<std::ptrdiff_t>(pos + p.hdr),
                      buf_.begin() + static_cast<std::ptrdiff_t>(pos + p.hdr + p.len));
    if (p.is_signed) {
      const std::size_t s = pos + p.hdr + p.len + 2;
      fr.has_signature = true;
      fr.link_id = buf_[s];
      fr.timestamp = 0;
      for (int i = 0; i < 6; ++i) {
        fr.timestamp |= static_cast<std::uint64_t>(buf_[s + 1 + i]) << (8 * i);
      }
      std::copy_n(buf_.begin() + static_cast<std::ptrdiff_t>(s + 7), 6, fr.signature.begin());
    }
    if (filter_ && !filter_(fr, std::span<const std::uint8_t>{&buf_[pos], p.total})) {
      ++stats_.filter_rejected;
      ++pos;  // as for a bad CRC: rescan after the STX byte (do not swallow the next frame)
      continue;
    }
    ++stats_.frames;
    if (out != nullptr) {
      out->push_back(std::move(fr));
    }
    pos += p.total;
  }
  buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos));
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
