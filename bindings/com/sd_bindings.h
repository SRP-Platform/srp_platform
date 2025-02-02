/**
 * @file sd_bindings.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-23
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef BINDINGS_COM_SD_BINDINGS_H_
#define BINDINGS_COM_SD_BINDINGS_H_

#include "platform/com/internal/sd_component.h"

namespace srp {
namespace bindings {
namespace com {
using SdComponentInterface = platform::com::sd::SdProxyComponent;
using SdComponentEntry = platform::com::sd::SdProxyComponent::SdEntry;
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COM_SD_BINDINGS_H_
