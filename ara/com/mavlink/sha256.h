/**
 * Copyright (c) 2024
 *
 * @file sha256.h
 * @brief SHA-256 i HMAC-SHA256 (bez zależności zewnętrznych) — podpis MAVLink 2.
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
  /// Kończy obliczenie; obiekt nie nadaje się do dalszego użycia.
  Sha256Digest Finalize();

  static Sha256Digest Hash(std::span<const std::uint8_t> data);

 private:
  void Block(const std::uint8_t* p);
  std::array<std::uint32_t, 8> h_;
  std::array<std::uint8_t, 64> buf_{};
  std::size_t buf_len_{0};
  std::uint64_t total_bits_{0};
};

class HmacSha256 {
 public:
  explicit HmacSha256(std::span<const std::uint8_t> key);
  void Update(std::span<const std::uint8_t> data) { inner_.Update(data); }
  Sha256Digest Finalize();

  static Sha256Digest Mac(std::span<const std::uint8_t> key,
                          std::span<const std::uint8_t> data);

 private:
  Sha256 inner_;
  std::array<std::uint8_t, 64> opad_{};
};

}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_SHA256_H_
