/**
 * @file memory_transport.h
 * @brief TESTONLY: transport bajtów w pamięci (para połączonych końców) z
 * wstrzykiwaniem błędów: opóźnienie, utrata, uszkodzenie, zapis częściowy,
 * błędy Open().
 * @copyright Copyright (c) 2024
 */
#ifndef ARA_COM_MAVLINK_TESTING_MEMORY_TRANSPORT_H_
#define ARA_COM_MAVLINK_TESTING_MEMORY_TRANSPORT_H_

#include <algorithm>
#include <atomic>
#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>  // NOLINT
#include <span>  // NOLINT
#include <stop_token>  // NOLINT
#include <string>
#include <utility>
#include <vector>

#include "ara/com/mavlink/i_byte_transport.h"
#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace testing {

using TestClock = std::chrono::steady_clock;

/// Jednokierunkowy kanał bajtów z pojemnością i opóźnieniem dostarczenia.
class MemoryWire {
 public:
  explicit MemoryWire(std::size_t capacity) : capacity_{capacity} {}

  /// Zwraca liczbę przyjętych bajtów (może być < size po timeout).
  std::size_t Push(const std::uint8_t* data, std::size_t size,
                   std::uint32_t timeout_ms, std::stop_token token,
                   const std::function<bool()>& closed) {
    std::unique_lock<std::mutex> lock{mutex_};
    const auto deadline = TestClock::now() + std::chrono::milliseconds(timeout_ms);
    std::size_t done = 0;
    while (done < size) {
      if (closed()) {
        return done;
      }
      const std::size_t free_space =
          capacity_ > queued_ ? capacity_ - queued_ : 0;
      if (free_space > 0) {
        const std::size_t n = std::min(free_space, size - done);
        Chunk c;
        c.ready = TestClock::now() + std::chrono::milliseconds(delay_ms_.load());
        c.bytes.assign(data + done, data + done + n);
        queued_ += n;
        done += n;
        chunks_.push_back(std::move(c));
        cv_.notify_all();
        continue;
      }
      if (timeout_ms == 0 || TestClock::now() >= deadline) {
        return done;
      }
      cv_.wait_until(lock, token, deadline, [&] {
        return closed() || queued_ < capacity_;
      });
      if (token.stop_requested()) {
        return done;
      }
    }
    return done;
  }

  /// 0 = timeout; -1 = zamknięte/przerwane.
  std::ptrdiff_t Pull(std::uint8_t* buf, std::size_t len, std::uint32_t timeout_ms,
            std::stop_token token, const std::function<bool()>& closed) {
    std::unique_lock<std::mutex> lock{mutex_};
    const auto deadline = TestClock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
      if (closed() || token.stop_requested()) {
        return -1;
      }
      const auto now = TestClock::now();
      std::size_t got = 0;
      while (got < len && !chunks_.empty() && chunks_.front().ready <= now) {
        auto& c = chunks_.front();
        const std::size_t n = std::min(len - got, c.bytes.size() - c.pos);
        std::copy_n(c.bytes.begin() + c.pos, n, buf + got);
        c.pos += n;
        got += n;
        queued_ -= n;
        if (c.pos == c.bytes.size()) {
          chunks_.pop_front();
        }
      }
      if (got > 0) {
        cv_.notify_all();
        return static_cast<std::ptrdiff_t>(got);
      }
      if (timeout_ms == 0 || now >= deadline) {
        return 0;
      }
      auto wake = deadline;
      if (!chunks_.empty()) {
        wake = std::min(wake, chunks_.front().ready);
      }
      cv_.wait_until(lock, token, wake, [&] { return closed(); });
    }
  }

  void Wake() {
    std::lock_guard<std::mutex> lock{mutex_};
    cv_.notify_all();
  }
  void Clear() {
    std::lock_guard<std::mutex> lock{mutex_};
    chunks_.clear();
    queued_ = 0;
    cv_.notify_all();
  }
  void SetDelayMs(std::uint32_t ms) { delay_ms_.store(ms); }
  void SetCapacity(std::size_t capacity) {
    std::lock_guard<std::mutex> lock{mutex_};
    capacity_ = capacity;
    cv_.notify_all();
  }
  std::size_t capacity() const { return capacity_; }

 private:
  struct Chunk {
    TestClock::time_point ready;
    std::vector<std::uint8_t> bytes;
    std::size_t pos{0};
  };
  std::mutex mutex_;
  std::condition_variable_any cv_;
  std::deque<Chunk> chunks_;
  std::size_t queued_{0};
  std::size_t capacity_;
  std::atomic<std::uint32_t> delay_ms_{0};
};

/// Jeden koniec pary. `Write` trafia do kanału tx_, `Read` czyta z rx_.
class MemoryTransport final : public IByteTransport {
 public:
  /// Filtr zapisu: może zmienić/obciąć bajty (pusty wynik = utrata).
  using WriteFilter = std::function<std::vector<std::uint8_t>(
      const std::vector<std::uint8_t>&)>;

  MemoryTransport(std::string name, std::shared_ptr<MemoryWire> rx,
                  std::shared_ptr<MemoryWire> tx)
      : name_{std::move(name)}, rx_{std::move(rx)}, tx_{std::move(tx)} {}

  /// Para połączonych końców (a.Write -> b.Read i odwrotnie).
  static std::pair<std::shared_ptr<MemoryTransport>,
                   std::shared_ptr<MemoryTransport>>
  MakePair(const std::string& name_a = "mem-a", const std::string& name_b = "mem-b",
           std::size_t capacity = 64 * 1024) {
    auto a_to_b = std::make_shared<MemoryWire>(capacity);
    auto b_to_a = std::make_shared<MemoryWire>(capacity);
    return {std::make_shared<MemoryTransport>(name_a, b_to_a, a_to_b),
            std::make_shared<MemoryTransport>(name_b, a_to_b, b_to_a)};
  }

  ara::core::Result<void> Open(std::stop_token token) noexcept override {
    ++open_calls_;
    if (open_delay_ms_.load() > 0) {
      std::mutex m;
      std::condition_variable_any cv;
      std::unique_lock<std::mutex> lock{m};
      cv.wait_for(lock, token, std::chrono::milliseconds(open_delay_ms_.load()),
                  [] { return false; });
    }
    if (open_failures_.load() > 0) {
      --open_failures_;
      return MakeTransportError(
          open_failure_persistent_.load() ? TransportErrc::kPersistent
                                          : TransportErrc::kRetryable,
          name_ + ": wstrzyknięty błąd Open");
    }
    closed_.store(false);
    open_.store(true);
    return {};
  }

  /// Wywołania rozpoczęte przed Close() kończą się, nawet gdy Open() wróci
  /// zanim zdążą się obudzić (epoka).
  std::function<bool()> AbortedFn() {
    const auto e = epoch_.load();
    return [this, e] { return closed_.load() || epoch_.load() != e; };
  }

  ara::core::Result<std::size_t> Read(std::span<std::uint8_t> buffer,
                                      std::uint32_t timeout_ms,
                                      std::stop_token token) noexcept override {
    if (!open_.load() || closed_.load()) {
      return MakeTransportError(TransportErrc::kClosed, name_ + ": zamknięty");
    }
    if (fail_io_.load()) {
      return MakeTransportError(TransportErrc::kPersistent, name_ + ": błąd I/O");
    }
    const std::ptrdiff_t n =
        rx_->Pull(buffer.data(), buffer.size(), timeout_ms, token, AbortedFn());
    if (n < 0) {
      return MakeTransportError(TransportErrc::kClosed, name_ + ": zamknięty");
    }
    return static_cast<std::size_t>(n);
  }

  ara::core::Result<std::size_t> Write(std::span<const std::uint8_t> data,
                                       std::uint32_t timeout_ms,
                                       std::stop_token token) noexcept override {
    if (!open_.load() || closed_.load()) {
      return MakeTransportError(TransportErrc::kClosed, name_ + ": zamknięty");
    }
    if (fail_io_.load()) {
      return MakeTransportError(TransportErrc::kPersistent, name_ + ": błąd I/O");
    }
    ++write_calls_;
    std::vector<std::uint8_t> bytes{data.begin(), data.end()};
    WriteFilter filter;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      filter = write_filter_;
    }
    if (filter) {
      bytes = filter(bytes);
    }
    const auto aborted = AbortedFn();
    const std::size_t pushed =
        tx_->Push(bytes.data(), bytes.size(), timeout_ms, token, aborted);
    if (aborted()) {
      return MakeTransportError(TransportErrc::kClosed, name_ + ": zamknięty");
    }
    bytes_written_ += pushed;
    // Zwracamy liczbę bajtów WEJŚCIOWYCH uznanych za przyjęte: gdy filtr
    // zmienił rozmiar, raportujemy pełny zapis (utrata jest "po drodze").
    if (filter) {
      return pushed == bytes.size() ? data.size() : std::min(pushed, data.size());
    }
    return pushed;
  }

  void Close() noexcept override {
    epoch_.fetch_add(1);
    closed_.store(true);
    open_.store(false);
    rx_->Wake();
    tx_->Wake();
  }

  std::string Name() const noexcept override { return name_; }

  // --- sterowanie testowe ---
  void FailNextOpens(int n, bool persistent = false) {
    open_failures_.store(n);
    open_failure_persistent_.store(persistent);
  }
  void SetOpenDelayMs(std::uint32_t ms) { open_delay_ms_.store(ms); }
  void SetFailIo(bool fail) { fail_io_.store(fail); }
  void SetWriteFilter(WriteFilter f) {
    std::lock_guard<std::mutex> lock{mutex_};
    write_filter_ = std::move(f);
  }
  /// Opóźnienie dostarczenia bajtów zapisanych przez TEN koniec.
  void SetDeliveryDelayMs(std::uint32_t ms) { tx_->SetDelayMs(ms); }
  /// Pojemność kanału zapisu (mała = łatwo o zapis częściowy po timeout).
  void SetTxCapacity(std::size_t bytes) { tx_->SetCapacity(bytes); }
  std::size_t tx_capacity() const { return tx_->capacity(); }
  void ClearRx() { rx_->Clear(); }

  int open_calls() const { return open_calls_.load(); }
  int write_calls() const { return write_calls_.load(); }
  std::size_t bytes_written() const { return bytes_written_.load(); }
  bool is_open() const { return open_.load() && !closed_.load(); }

 private:
  const std::string name_;
  std::shared_ptr<MemoryWire> rx_;
  std::shared_ptr<MemoryWire> tx_;
  std::atomic<std::uint64_t> epoch_{0};
  std::atomic<bool> closed_{false};
  std::atomic<bool> open_{false};
  std::atomic<int> open_failures_{0};
  std::atomic<bool> open_failure_persistent_{false};
  std::atomic<std::uint32_t> open_delay_ms_{0};
  std::atomic<bool> fail_io_{false};
  std::atomic<int> open_calls_{0};
  std::atomic<int> write_calls_{0};
  std::atomic<std::size_t> bytes_written_{0};
  std::mutex mutex_;
  WriteFilter write_filter_;
};

}  // namespace testing
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_TESTING_MEMORY_TRANSPORT_H_
