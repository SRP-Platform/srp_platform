/**
 * @file proccess_socket.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-26
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "bindings/common/socket/proccess_socket.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <future>  // NOLINT
#include <utility>

#include "ara/com/com_error_domain.h"

namespace srp {
namespace bindings {
namespace com {
ProccessSocket::ProccessSocket(/* args */)
    : local_pid_{static_cast<uint32_t>(getpid())},
      local_soc_{"/run/p-" + std::to_string(local_pid_)} {}
ProccessSocket::ProccessSocket(const uint32_t app_id)
    : local_pid_{app_id}, local_soc_{"/run/p-" + std::to_string(local_pid_)} {}
ProccessSocket::ProccessSocket(const std::string& sock_path_)
    : local_pid_{static_cast<uint32_t>(getpid())},
      local_soc_{"/run/" + sock_path_} {}

ProccessSocket::~ProccessSocket() {}

void ProccessSocket::SetCallback(RxCallback&& callback) {
  callback_ = std::move(callback);
}

ara::core::Result<void> ProccessSocket::Offer() noexcept {
  const char* sock_path = local_soc_.c_str();
  if (remove(sock_path) == -1 && errno != ENOENT) {
    return ara::com::MakeErrorCode(ara::com::ComErrc::kUnsetFailure, "Error: remove failed");
  }

  memset(&addr_, 0x0, sizeof(struct sockaddr_un));
  addr_.sun_family = AF_UNIX;
  strncpy(addr_.sun_path, sock_path, sizeof(addr_.sun_path) - 1);

  sfd_ = ::socket(AF_UNIX, SOCK_DGRAM, 0);
  if (sfd_ == -1) {
    return ara::com::MakeErrorCode(ara::com::ComErrc::kUnsetFailure, "Error: socket failed");
  }
  {
    int reuse = 1;

    if (setsockopt(sfd_, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse,  // NOLINT
                   sizeof(reuse)) < 0) {
      return ara::com::MakeErrorCode(ara::com::ComErrc::kUnsetFailure,
                           "Error: setsockopt failed (SO_REUSEADDR)");
    }
  }
  if (bind(sfd_, (struct sockaddr*)&addr_, sizeof(struct sockaddr_un)) == -1) {
    close(sfd_);
    return ara::com::MakeErrorCode(ara::com::ComErrc::kUnsetFailure, "Error: bind failed");
  }
  rx_thread_ = std::make_unique<std::jthread>(
      [this](std::stop_token token) { this->RxLoop(token); });
      pthread_setname_np(rx_thread_->native_handle(),"PROCESS_SOCK");
  return {};
}
ara::core::Result<void> ProccessSocket::StopOffer() noexcept {
  if (rx_thread_ == nullptr) {
    return ara::com::MakeErrorCode(ara::com::ComErrc::kServiceNotOffered, "");
  }
  rx_thread_.release();
  close(sfd_);
  return {};
}
ara::core::Result<void> ProccessSocket::TransmitToPid(
    uint16_t dest_pid, const std::vector<uint8_t>& payload) noexcept {
  return this->Transmit(std::string{"p-" + std::to_string(dest_pid)}, payload);
}

ara::core::Result<void> ProccessSocket::Transmit(
    const std::string& path, const std::vector<uint8_t>& payload) noexcept {
  const std::lock_guard<std::mutex> lock{sending_m_};
  struct sockaddr_un remote;
  memset(&remote, 0, sizeof(struct sockaddr_un));

  remote.sun_family = AF_UNIX;
  strcpy(remote.sun_path, ("/run/" + path).c_str());  // NOLINT

  std::uint8_t* buffor = new std::uint8_t[payload.size() + sizeof(int)];

  std::memcpy(buffor, &local_pid_, sizeof(local_pid_));
  std::copy(payload.begin(), payload.end(), buffor + sizeof(int));

  const auto rc = sendto(sfd_, buffor, payload.size() + sizeof(int), 0,
                         (struct sockaddr*)&remote, sizeof(remote));
  delete[] buffor;
  if (rc == -1) {
    return ara::com::MakeErrorCode(ara::com::ComErrc::kServiceNotOffered, "Sanding Failed");
  }
  return {};
}

void ProccessSocket::RxLoop(std::stop_token token) noexcept {
  const std::stop_callback stop_wait{
      token, [this]() { shutdown(this->sfd_, SHUT_RD); }};
  while (true) {
    std::array<char, 256 * 2> buffor;
    int bytes_rec =
        read(sfd_, reinterpret_cast<char*>(&buffor), 256 * 2);  // NOLINT
    if (bytes_rec > 0) {
      if (bytes_rec >= static_cast<int>(sizeof(uint32_t))) {
        uint32_t pid{0};
        std::memcpy(&pid, buffor.data(), sizeof(uint32_t));
        if (this->callback_) {
          if (buffor.size() > 0) {
            std::ignore = std::async(
                std::launch::async, [this, &pid, &buffor, &bytes_rec]() {
                  this->callback_(
                      pid, std::vector<uint8_t>{buffor.begin() + sizeof(uint32_t),
                                                buffor.begin() + (bytes_rec)});
                });
          }
        }
      }
    }
    if (token.stop_requested()) {
      break;
    }
  }
}
}  // namespace com
}  // namespace bindings
}  // namespace srp
