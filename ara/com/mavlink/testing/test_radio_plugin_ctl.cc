/**
 * Copyright (c) 2024
 *
 * @file test_radio_plugin_ctl.cc
 * @brief TESTONLY: plugin `test_ctl_radio` — SerialRadio na pty sterowany plikami (testy E2E):
 *  - istnienie "hold_file": Send() zwraca kBusy; po usunięciu pliku driver woła OnTxReady();
 *  - istnienie "fatal_file": plik jest usuwany i driver zgłasza OnError(fatal).
 * Config (JSON): {"device": "...", "baudrate": 115200, "hold_file": "...", "fatal_file": "..."}.
 */
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>  // NOLINT
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <thread>  // NOLINT

#include "ara/com/mavlink/radio_driver.h"
#include "ara/com/mavlink/serial_radio.h"
#include "core/json/json_parser.h"

namespace {
using ara::com::mavlink::IRadioDriver;
using ara::com::mavlink::IRadioListener;
using ara::com::mavlink::MakeTransportError;
using ara::com::mavlink::TransportErrc;

bool Exists(const std::string& path) {
  struct stat st {};
  return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

class CtlRadio final : public IRadioDriver, private IRadioListener {
 public:
  explicit CtlRadio(const std::string& config_path) : inner_{config_path} {
    const auto json = srp::core::json::JsonParser::Parser(config_path);
    if (json.has_value()) {
      hold_file_ = json->GetString("hold_file").value_or("");
      fatal_file_ = json->GetString("fatal_file").value_or("");
    }
  }
  ~CtlRadio() override { Stop(); }

  ara::core::Result<void> Start(IRadioListener& l, std::stop_token token) noexcept override {  // NOLINT
    std::lock_guard<std::mutex> life{lifecycle_};
    listener_ = &l;
    const auto r = inner_.Start(*this, token);
    if (!r.HasValue()) {
      return r;
    }
    stop_ = false;
    control_ = std::thread([this] { Control(); });
    return {};
  }

  void Stop() noexcept override {
    std::lock_guard<std::mutex> life{lifecycle_};
    stop_ = true;
    if (control_.joinable()) {
      control_.join();
    }
    inner_.Stop();
  }

  ara::core::Result<void> Send(std::span<const std::uint8_t> frame) noexcept override {
    if (Exists(hold_file_)) {
      busy_owed_ = true;
      return MakeTransportError(TransportErrc::kBusy, "test_ctl_radio: hold_file");
    }
    return inner_.Send(frame);
  }

  std::string Name() const noexcept override { return "test_ctl_radio"; }

 private:
  // IRadioListener dla wewnętrznego SerialRadio: przekazujemy dalej
  void OnReceive(std::span<const std::uint8_t> b) noexcept override { listener_->OnReceive(b); }
  void OnTxReady() noexcept override { listener_->OnTxReady(); }
  void OnError(ara::core::ErrorCode e, bool fatal) noexcept override { listener_->OnError(e, fatal); }

  void Control() {
    while (!stop_.load()) {
      if (busy_owed_.load() && !Exists(hold_file_)) {
        busy_owed_ = false;
        listener_->OnTxReady();
      }
      if (Exists(fatal_file_)) {
        ::unlink(fatal_file_.c_str());
        listener_->OnError(MakeTransportError(TransportErrc::kPersistent, "test_ctl_radio: fatal_file"), true);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  ara::com::mavlink::SerialRadio inner_;
  std::string hold_file_;
  std::string fatal_file_;
  IRadioListener* listener_{nullptr};
  std::atomic<bool> busy_owed_{false};
  std::atomic<bool> stop_{false};
  std::mutex lifecycle_;
  std::thread control_;
};

void RegisterDrivers(ara::com::mavlink::IRadioDriverRegistry& registry) {  // NOLINT(runtime/references)
  registry.Register("test_ctl_radio", [](const std::string& cfg) {
    return std::shared_ptr<IRadioDriver>{std::make_shared<CtlRadio>(cfg)};
  });
}

}  // namespace

SRP_MAVLINK_DRIVER_PLUGIN(RegisterDrivers)
