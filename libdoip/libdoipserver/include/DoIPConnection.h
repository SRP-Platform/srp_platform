// NOLINTBEGIN
#ifndef DOIPCONNECTION_H
#define DOIPCONNECTION_H

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

#include "AliveCheckTimer.h"
#include "DiagnosticMessageHandler.h"
#include "DoIPGenericHeaderHandler.h"
#include "RoutingActivationHandler.h"
#include "VehicleIdentificationHandler.h"

using CloseConnectionCallback = std::function<void()>;

const unsigned long _MaxDataSize = 0xFFFFFF;

class DoIPConnection {
 public:
  DoIPConnection(int tcpSocket, unsigned short logicalGatewayAddress)
      : tcpSocket(tcpSocket), logicalGatewayAddress(logicalGatewayAddress){};

  int receiveTcpMessage();
  unsigned long receiveFixedNumberOfBytesFromTCP(unsigned long payloadLength,
                                                 unsigned char* receivedData);

  void sendDiagnosticPayload(unsigned short sourceAddress, unsigned char* data,
                             int length);
  bool isSocketActive() { return tcpSocket != 0; };

  void triggerDisconnection();

  void sendDiagnosticAck(unsigned short sourceAddress, bool ackType,
                         unsigned char ackCode);
  int sendNegativeAck(unsigned char ackCode);

  void setCallback(DiagnosticCallback dc, DiagnosticMessageNotification dmn,
                   CloseConnectionCallback ccb);
  void setGeneralInactivityTime(const uint16_t seconds);

 private:
  int tcpSocket;

  AliveCheckTimer aliveCheckTimer;
  DiagnosticCallback diag_callback;
  CloseConnectionCallback close_connection;
  DiagnosticMessageNotification notify_application;

  unsigned char* routedClientAddress;
  unsigned short logicalGatewayAddress = 0x0000;

  void closeSocket();

  int reactOnReceivedTcpMessage(GenericHeaderAction action,
                                unsigned long payloadLength,
                                unsigned char* payload);

  int sendMessage(unsigned char* message, int messageLenght);

  void aliveCheckTimeout();
};

#endif /* DOIPCONNECTION_H */
// NOLINTEND
