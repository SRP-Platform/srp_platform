/**
 * @file ipc_socket.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2023-11-14
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "bindings/common/socket/ipc_socket.h"

#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "ara/com/com_error_domain.h"

namespace srp {
namespace bindings {
namespace com {
namespace soc {
bool IpcSocket::SocketExist(const std::string path) {
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0);
}

ara::core::Result<void> IpcSocket::Init(const std::string& socket_path) {
  memset(&server_sockaddr, 0, sizeof(struct sockaddr_un));
  server_sock = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (server_sock == -1) {
    return ara::com::MakeErrorCode(ara::com::ComErrc::kNetworkBindingFailure,
                                   "");
  }
  umask(0);
  server_sockaddr.sun_family = AF_UNIX;
  if (SocketExist("/run/" + socket_path)) {
    remove(("/run/" + socket_path).c_str());
  }
  strcpy(server_sockaddr.sun_path,          // NOLINT
         ("/run/" + socket_path).c_str());  // NOLINT
  len = sizeof(server_sockaddr);
  unlink(("/run/" + socket_path).c_str());
  return {};
}

void IpcSocket::SetRXCallback(RXCallback callback) {
  this->callback_ = callback;
}

ara::core::Result<void> IpcSocket::Transmit(const std::string& ip,
                                            std::vector<std::uint8_t> payload) {
  int client_socket, rc;
  struct sockaddr_un remote;
  memset(&remote, 0, sizeof(struct sockaddr_un));
  client_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (client_socket == -1) {
    return ara::com::MakeErrorCode(ara::com::ComErrc::kNetworkBindingFailure,
                                   "");
  }

  remote.sun_family = AF_UNIX;
  strcpy(remote.sun_path, ("/run/" + ip).c_str());  // NOLINT

  std::uint8_t* buffor = new std::uint8_t[payload.size()];
  std::copy(payload.begin(), payload.end(), buffor);

  rc = sendto(client_socket, buffor, payload.size(), 0,
              (struct sockaddr*)&remote, sizeof(remote));
  delete[] buffor;
  close(client_socket);
  if (rc == -1) {
    return ara::com::MakeErrorCode(ara::com::ComErrc::kCommunicationStackError,
                                   "");
  }

  return {};
}

void IpcSocket::StartRXThread() {
  if (rx_thred != nullptr) {
    return;
  }
  this->rx_thred = std::make_unique<std::jthread>(
      [&](std::stop_token stoken) { this->Loop(stoken); });
  pthread_setname_np(this->rx_thred->native_handle(), "IPC_SOCK_RX");
}

void IpcSocket::Loop(std::stop_token stoken) {
  rc = bind(server_sock, (struct sockaddr*)&server_sockaddr, len);
  if (rc == -1) {
    return;
  }
  const std::stop_callback stop_wait{
      stoken, [this]() { shutdown(this->server_sock, SHUT_RD); }};
  while (true) {
    std::array<char, 256 * 2> buffor;
    bytes_rec =
        read(server_sock, reinterpret_cast<char*>(&buffor), 256 * 2);  // NOLINT
    if (bytes_rec > 0) {
      if (this->callback_) {
        if (buffor.size() > 0) {
          this->callback_(
              std::vector<uint8_t>{buffor.begin(), buffor.begin() + bytes_rec});
        }
      }
    }
    if (stoken.stop_requested()) {
      break;
    }
  }
  // close(server_sock);
}

void IpcSocket::StopRXThread() {
  this->rx_thred->request_stop();
  this->rx_thred->join();
}
IpcSocket::~IpcSocket() {
  this->rx_thred->request_stop();
  this->rx_thred->join();
}
}  // namespace soc
}  // namespace com
}  // namespace bindings
}  // namespace srp
