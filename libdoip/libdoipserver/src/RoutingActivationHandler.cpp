// NOLINTBEGIN
#include "RoutingActivationHandler.h"

/**
 * Checks if the Routing Activation Request is valid
 * @pplatformm data  contains the request
 * @return      routing activation response code
 */
unsigned char parseRoutingActivation(unsigned char* data) {
  // Check if source address is known
  uint32_t address = 0;
  address |= (uint32_t)data[0] << 8;
  address |= (uint32_t)data[1];
  if (!checkSourceAddress(address)) {
    // send routing activation negative response code --> close socket
    return _UnknownSourceAddressCode;
  }

  // Check if routing activation type is supported
  switch (data[2]) {
    case 0x00: {
      // Activation Type default
      break;
    }
    case 0x01: {
      // Activation Type WWH-OBD
      break;
    }
    default: {
      // unknown activation type
      // send routing activation negative response code --> close socket
      return _UnsupportedRoutingTypeCode;
    }
  }

  // if not exited before, send routing activation positive response
  return _SuccessfullyRoutedCode;
}

/**
 * Create the complete routing activation response, which also contains the
 * generic header
 * @pplatformm clientAddress     address of the test equipment
 * @pplatformm responseCode      routing activation response code
 * @return                  complete routing activation response
 */
unsigned char* createRoutingActivationResponse(unsigned short sourceAddress,
                                               unsigned char clientAddress[2],
                                               unsigned char responseCode) {
  unsigned char* message = createGenericHeader(
      PayloadType::ROUTINGACTIVATIONRESPONSE, _ActivationResponseLength);

  // Logical address of external test equipment
  message[8] = clientAddress[0];
  message[9] = clientAddress[1];

  // logical address of DoIP entity
  message[10] = (unsigned char)((sourceAddress >> 8) & 0xFF);
  message[11] = (unsigned char)(sourceAddress & 0xFF);

  // routing activation response code
  message[12] = responseCode;

  // reserved for future standardization use
  message[13] = 0x00;
  message[14] = 0x00;
  message[15] = 0x00;
  message[16] = 0x00;

  return message;
}

/**
 * Checks if the submitted address is valid
 * @pplatformm address	the address which will be checked
 * @return			true if address is valid
 */
bool checkSourceAddress(uint32_t address) {
  uint32_t minAddress = 3584;  // 0x0E00
  uint32_t maxAddress = 4095;  // 0x0FFF

  for (uint32_t i = minAddress; i <= maxAddress; i++) {
    if (address == i) {
      return true;
    }
  }

  return false;
}
// NOLINTEND
