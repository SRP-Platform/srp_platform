/**
 * Copyright (c) 2024
 *
 * @file sha256.h
 * @brief SHA-256 without external dependencies; MAVLink 2 signing is a plain SHA-256 over
 * secret key + frame.
 */
#ifndef ARA_COM_MAVLINK_SHA256_H_
#define ARA_COM_MAVLINK_SHA256_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>  // NOLINT

namespace ara {
namespace com {
namespace mavlink {

using Sha256Digest = std::array<std::uint8_t, 32>;

class Sha256 {
 public:
  Sha256();
  void Update(std::span<const std::uint8_t> data);
  /// Finishes the computation; the object must not be used afterwards.
  Sha256Digest Finalize();

 private:
  void Block(const std::uint8_t* p);
  std::array<std::uint32_t, 8> h_;
  std::array<std::uint8_t, 64> buf_{};
  std::size_t buf_len_{0};
  std::uint64_t total_bits_{0};
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_SHA256_H_
