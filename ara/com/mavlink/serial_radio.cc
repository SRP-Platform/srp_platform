/**
 * Copyright (c) 2024
 *
 * @file serial_radio.cc
 */
#include "ara/com/mavlink/serial_radio.h"

#include <cerrno>
#include <cstring>
#include <memory>
#include <utility>

#include "core/json/json_parser.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using srp::core::serial::IoStatus;

constexpr std::uint32_t kPollTimeoutMs = 500;  // tylko okresowe sprawdzenie stop_token

TransportErrc ClassifyOpenError(int err, bool busy) {
  if (busy) {
    return TransportErrc::kPersistent;
  }
  switch (err) {
    case ENOENT:
    case ENODEV:
    case ENXIO:
    case EACCES:
    case EPERM:
    case EBUSY:
    case EINVAL:
    case ENOTTY:
    case EISDIR:
    case ENOTDIR:
    case ELOOP:
    case ENAMETOOLONG:
      return TransportErrc::kPersistent;
    default:
      return TransportErrc::kRetryable;
  }
}
}  // namespace

SerialRadio::SerialRadio(std::string name, std::string device, std::uint32_t baudrate)
    : name_{std::move(name)}, device_{std::move(device)}, baudrate_{baudrate} {}

SerialRadio::SerialRadio(const std::string& config_path)
    : name_{kSerialRadioId}, config_path_{config_path} {
  const auto json = srp::core::json::JsonParser::Parser(config_path);
  if (!json.has_value()) {
    configured_ = false;
    config_error_ = "serial_radio: nie można wczytać pliku konfiguracji '" + config_path + "'";
    return;
  }
  const auto device = json->GetString("device");
  const auto baud = json->GetNumber<std::uint32_t>("baudrate");
  if (!device.has_value() || device->empty() || !baud.has_value()) {
    configured_ = false;
    config_error_ = "serial_radio: plik '" + config_path +
                    "' musi zawierać \"device\" (string) i \"baudrate\" (liczba)";
    return;
  }
  if (!srp::core::serial::SerialPort::IsBaudrateSupported(*baud)) {
    configured_ = false;
    config_error_ = "serial_radio: baudrate " + std::to_string(*baud) + " nieobsługiwany (plik '" + config_path + "')";
    return;
  }
  device_ = *device;
  baudrate_ = *baud;
  name_ = std::string(kSerialRadioId) + "(" + device_ + ")";
}

SerialRadio::~SerialRadio() { Stop(); }

ara::core::Result<void> SerialRadio::Start(IRadioListener& listener, std::stop_token token) noexcept {
  std::lock_guard<std::mutex> life{lifecycle_mutex_};
  if (!configured_) {
    return MakeTransportError(TransportErrc::kPersistent, config_error_);
  }
  if (thread_.joinable()) {
    return MakeTransportError(TransportErrc::kInvalidArgument, name_ + ": Start() bez Stop()");
  }
  if (token.stop_requested()) {
    return MakeTransportError(TransportErrc::kClosed, name_ + ": Start przerwany");
  }
  const auto res = port_.Open(device_, baudrate_);
  if (!res.ok) {
    return MakeTransportError(ClassifyOpenError(res.err, res.busy), name_ + ": " + res.message);
  }
  {
    std::lock_guard<std::mutex> lock{mutex_};
    tx_tail_.clear();
    tx_ready_owed_ = false;
    running_ = true;
  }
  stop_ = std::stop_source{};
  try {
    thread_ = std::thread{[this, token, l = &listener] { Loop(token, l); }};
  } catch (...) {
    port_.Close();
    std::lock_guard<std::mutex> lock{mutex_};
    running_ = false;
    return MakeTransportError(TransportErrc::kRetryable, name_ + ": nie udało się uruchomić wątku");
  }
  return {};
}

void SerialRadio::Stop() noexcept {
  std::lock_guard<std::mutex> life{lifecycle_mutex_};
  {
    std::lock_guard<std::mutex> lock{mutex_};
    running_ = false;
  }
  stop_.request_stop();
  port_.Close();
  if (thread_.joinable()) {
    thread_.join();
  }
}

ara::core::Result<void> SerialRadio::Send(std::span<const std::uint8_t> frame) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  if (!running_) {
    return MakeTransportError(TransportErrc::kClosed, name_ + ": zatrzymany");
  }
  if (frame.empty()) {
    return {};
  }
  if (!tx_tail_.empty()) {
    tx_ready_owed_ = true;  // wątek dopisuje resztę poprzedniej ramki; OnTxReady po jej zapisie
    return MakeTransportError(TransportErrc::kBusy, name_ + ": dopisywanie reszty poprzedniej ramki");
  }
  const auto r = port_.TryWrite(frame.data(), frame.size());
  switch (r.status) {
    case IoStatus::kOk:
      if (r.bytes < frame.size()) {
        tx_tail_.assign(frame.begin() + static_cast<std::ptrdiff_t>(r.bytes), frame.end());
        port_.Kick();  // wątek ma zacząć czekać na POLLOUT
      }
      return {};
    case IoStatus::kTimeout:
      tx_ready_owed_ = true;
      port_.Kick();
      return MakeTransportError(TransportErrc::kBusy, name_ + ": bufor UART pełny");
    case IoStatus::kClosed:
      return MakeTransportError(TransportErrc::kClosed, name_ + ": zamknięty");
    case IoStatus::kError:
      break;
  }
  return MakeTransportError(TransportErrc::kPersistent,
                            name_ + ": błąd zapisu: " + std::string{std::strerror(r.err)});
}

bool SerialRadio::FlushTail(IRadioListener* listener) {
  bool ready = false;
  std::string fatal;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    while (!tx_tail_.empty() && fatal.empty()) {
      const auto r = port_.TryWrite(tx_tail_.data(), tx_tail_.size());
      if (r.status == IoStatus::kOk) {
        tx_tail_.erase(tx_tail_.begin(), tx_tail_.begin() + static_cast<std::ptrdiff_t>(r.bytes));
      } else if (r.status == IoStatus::kError) {
        fatal = name_ + ": błąd zapisu: " + std::string{std::strerror(r.err)};
      } else {
        return true;  // kTimeout (bufor jeszcze pełny) albo kClosed: reszta zostaje
      }
    }
    if (fatal.empty() && tx_ready_owed_) {
      tx_ready_owed_ = false;
      ready = true;
    }
  }
  // Callbacki poza mutexem: listener może wołać Send() (np. po OnTxReady).
  if (!fatal.empty()) {
    listener->OnError(MakeTransportError(TransportErrc::kPersistent, fatal), true);
    return false;
  }
  if (ready) {
    listener->OnTxReady();
  }
  return true;
}

void SerialRadio::Loop(std::stop_token caller, IRadioListener* listener) {
  const std::stop_token st = stop_.get_token();
  // Przerwanie przez token z Start() (np. zatrzymanie daemona) kończy wątek tak samo jak Stop().
  std::stop_callback on_caller_stop{caller, [src = stop_]() mutable { src.request_stop(); }};
  std::uint8_t buf[512];
  while (!st.stop_requested()) {
    bool want_write;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      want_write = !tx_tail_.empty() || tx_ready_owed_;
    }
    const auto pr = port_.Poll(want_write, kPollTimeoutMs, st);
    if (pr.status == IoStatus::kClosed) {
      break;
    }
    if (pr.status == IoStatus::kError) {
      listener->OnError(MakeTransportError(TransportErrc::kPersistent,
                                           name_ + ": utrata urządzenia: " + std::string{std::strerror(pr.err)}),
                        true);
      break;
    }
    if (pr.readable) {
      for (;;) {
        const auto r = port_.Read(buf, sizeof(buf), 0, st);
        if (r.status == IoStatus::kOk && r.bytes > 0) {
          listener->OnReceive(std::span<const std::uint8_t>(buf, r.bytes));
          if (st.stop_requested()) {
            break;
          }
          continue;
        }
        if (r.status == IoStatus::kError) {
          listener->OnError(MakeTransportError(TransportErrc::kPersistent,
                                               name_ + ": błąd odczytu: " + std::string{std::strerror(r.err)}),
                            true);
          return;
        }
        break;  // kTimeout (brak danych) / kClosed
      }
    }
    if (st.stop_requested()) {
      break;
    }
    if (pr.writable || pr.kicked) {
      if (!FlushTail(listener)) {
        return;
      }
    }
  }
}

RadioDriverFactory SerialRadioFactory() {
  return [](const std::string& config_path) {
    return std::shared_ptr<IRadioDriver>{std::make_shared<SerialRadio>(config_path)};
  };
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
