/**
 * @file ipc_socket.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2023-11-13
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef BINDINGS_COMMON_SOCKET_IPC_SOCKET_H_
#define BINDINGS_COMMON_SOCKET_IPC_SOCKET_H_
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "bindings/common/socket/Isocket.h"

namespace srp {
namespace bindings {
namespace com {
namespace soc {
class IpcSocket : public ISocket {
 private:
  int server_sock, len, rc;
  int bytes_rec = 0;
  struct sockaddr_un server_sockaddr, peer_sock;

  std::unique_ptr<std::jthread> rx_thred;
  RXCallback callback_;
  bool SocketExist(const std::string path);
  void Loop(std::stop_token stoken);

 public:
  /**
   * @brief
   *
   * @param socket_path
   * @return ara::core::Result<void>
   */
  ara::core::Result<void> Init(const std::string& socket_path) override;
  /**
   * @brief Setter for rx callback
   *
   * @param callback
   */
  void SetRXCallback(RXCallback callback) override;
  /**
   * @brief Function to send data by socket
   *
   * @param ip target ip or path
   * @param payload payload to send
   * @return core::ErrorCode status
   */
  ara::core::Result<void> Transmit(const std::string& ip,
                                   std::vector<std::uint8_t> payload) override;
  /**
   * @brief This function start RX thread
   *
   */
  ~IpcSocket();
  void StartRXThread() override;
  void StopRXThread() override;
};
}  // namespace soc
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COMMON_SOCKET_IPC_SOCKET_H_
