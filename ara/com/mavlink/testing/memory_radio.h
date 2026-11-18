/**
 * Copyright (c) 2024
 *
 * @file memory_radio.h
 * @brief TESTONLY: driver radia w pamięci, sterowany z testu. Spełnia kontrakt IRadioDriver
 * (callbacki z własnego wątku, Send nieblokujące, kBusy + OnTxReady, zero callbacków po Stop()).
 *
 * Sterowanie: Inject() (bajty „z eteru” -> OnReceive), SetHold(true) (Send zwraca kBusy aż do
 * SetHold(false), wtedy OnTxReady), Fatal() (OnError fatal), FailStarts(n) (Start() się nie udaje),
 * sent() / WaitSent() (ramki przyjęte przez Send(), jedna ramka = jeden element).
 */
#ifndef ARA_COM_MAVLINK_TESTING_MEMORY_RADIO_H_
#define ARA_COM_MAVLINK_TESTING_MEMORY_RADIO_H_

#include <algorithm>
#include <atomic>
#include <chrono>  // NOLINT
#include <condition_variable>  // NOLINT
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "ara/com/mavlink/radio_driver.h"
#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace testing {

class MemoryRadio final : public IRadioDriver {
 public:
  explicit MemoryRadio(std::string name = "memory_radio") : name_{std::move(name)} {}
  ~MemoryRadio() override { Stop(); }

  ara::core::Result<void> Start(IRadioListener& listener, std::stop_token) noexcept override {  // NOLINT
    std::lock_guard<std::mutex> life{lifecycle_mutex_};
    {
      std::lock_guard<std::mutex> lock{mutex_};
      ++start_attempts_;
      if (fail_starts_ > 0) {
        --fail_starts_;
        return MakeTransportError(fail_persistent_ ? TransportErrc::kPersistent : TransportErrc::kRetryable,
                                  name_ + ": start odrzucony (test)");
      }
      if (worker_.joinable()) {
        return MakeTransportError(TransportErrc::kInvalidArgument, name_ + ": Start() bez Stop()");
      }
      listener_ = &listener;
      running_ = true;
      stopping_ = false;
      busy_owed_ = false;
      tasks_.clear();
      ++start_count_;
    }
    worker_ = std::thread([this] { Worker(); });
    return {};
  }

  void Stop() noexcept override {
    std::lock_guard<std::mutex> life{lifecycle_mutex_};
    {
      std::lock_guard<std::mutex> lock{mutex_};
      running_ = false;
      stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
      std::lock_guard<std::mutex> lock{mutex_};
      ++stop_count_;
    }
  }

  ara::core::Result<void> Send(std::span<const std::uint8_t> frame) noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!running_) {
      return MakeTransportError(TransportErrc::kClosed, name_ + ": zatrzymany");
    }
    if (hold_) {
      busy_owed_ = true;
      ++busy_count_;
      return MakeTransportError(TransportErrc::kBusy, name_ + ": wstrzymany (test)");
    }
    sent_.emplace_back(frame.begin(), frame.end());
    sent_cv_.notify_all();
    return {};
  }

  std::string Name() const noexcept override { return name_; }

  // ------------------------------------------------------------- sterowanie z testu

  /// Bajty „z eteru”: OnReceive z wątku drivera, porcjami po `chunk` (0 = jedna porcja).
  void Inject(const std::vector<std::uint8_t>& bytes, std::size_t chunk = 0) {
    const std::size_t step = chunk == 0 ? std::max<std::size_t>(bytes.size(), 1) : chunk;
    for (std::size_t i = 0; i < bytes.size(); i += step) {
      std::vector<std::uint8_t> part(bytes.begin() + static_cast<std::ptrdiff_t>(i),
                                     bytes.begin() + static_cast<std::ptrdiff_t>(std::min(bytes.size(), i + step)));
      Post([part](IRadioListener* l) { l->OnReceive(part); });
    }
  }

  void SetHold(bool hold) {
    bool ready = false;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      hold_ = hold;
      if (!hold && busy_owed_) {
        busy_owed_ = false;
        ready = true;
      }
    }
    if (ready) {
      Post([](IRadioListener* l) { l->OnTxReady(); });
    }
  }

  void Fatal(const std::string& text = "test fatal") {
    Post([text](IRadioListener* l) {
      l->OnError(MakeTransportError(TransportErrc::kPersistent, text), true);
    });
  }

  void NonFatal(const std::string& text = "test warning") {
    Post([text](IRadioListener* l) {
      l->OnError(MakeTransportError(TransportErrc::kRetryable, text), false);
    });
  }

  /// Następne `n` wywołań Start() zwróci błąd (trwały albo przejściowy).
  void FailStarts(int n, bool persistent = true) {
    std::lock_guard<std::mutex> lock{mutex_};
    fail_starts_ = n;
    fail_persistent_ = persistent;
  }

  std::vector<std::vector<std::uint8_t>> sent() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return sent_;
  }
  std::size_t sent_count() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return sent_.size();
  }
  void ClearSent() {
    std::lock_guard<std::mutex> lock{mutex_};
    sent_.clear();
  }
  /// Czeka, aż `sent()` ma >= n ramek.
  bool WaitSent(std::size_t n, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock{mutex_};
    return sent_cv_.wait_for(lock, timeout, [&] { return sent_.size() >= n; });
  }
  int start_count() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return start_count_;
  }
  int start_attempts() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return start_attempts_;
  }
  int stop_count() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return stop_count_;
  }
  int busy_count() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return busy_count_;
  }

 private:
  using Task = std::function<void(IRadioListener*)>;

  void Post(Task t) {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      if (!running_) {
        return;
      }
      tasks_.push_back(std::move(t));
    }
    cv_.notify_all();
  }

  void Worker() {
    for (;;) {
      Task t;
      IRadioListener* l = nullptr;
      {
        std::unique_lock<std::mutex> lock{mutex_};
        cv_.wait(lock, [&] { return stopping_ || !tasks_.empty(); });
        if (stopping_) {
          return;
        }
        t = std::move(tasks_.front());
        tasks_.pop_front();
        l = listener_;
      }
      t(l);
    }
  }

  std::string name_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::condition_variable sent_cv_;
  std::mutex lifecycle_mutex_;
  std::thread worker_;
  IRadioListener* listener_{nullptr};
  std::deque<Task> tasks_;
  std::vector<std::vector<std::uint8_t>> sent_;
  bool running_{false};
  bool stopping_{false};
  bool hold_{false};
  bool busy_owed_{false};
  int fail_starts_{0};
  bool fail_persistent_{true};
  int start_count_{0};
  int start_attempts_{0};
  int stop_count_{0};
  int busy_count_{0};
};

}  // namespace testing
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_TESTING_MEMORY_RADIO_H_
