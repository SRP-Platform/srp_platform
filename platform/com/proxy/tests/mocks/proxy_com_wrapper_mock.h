/**
 * @file i_proxy_com_wrapper.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-23
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef PLATFORM_COM_PROXY_TESTS_MOCKS_PROXY_COM_WRAPPER_MOCK_H_
#define PLATFORM_COM_PROXY_TESTS_MOCKS_PROXY_COM_WRAPPER_MOCK_H_
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "platform/com/msg_type.h"
#include "platform/com/proxy/i_proxy_com_wrapper.h"

namespace platform {
namespace com {
namespace proxy {
namespace wrapper {
class ProxyComWrapperMock : public IProxyComWrapper {
 public:
  MOCK_METHOD(void, ProceedFrame, (const platform::com::IpcMsg&),
              (noexcept, override));
  MOCK_METHOD(bool, TransmitFrame, (const platform::com::IpcMsg&),
              (noexcept, override));
  MOCK_METHOD(void, SetProceedFrameCallback, (const IProxyComWrapper::ProceedFrameCallback&&),
              (noexcept, override));
  virtual ~ProxyComWrapperMock() = default;
};
}  // namespace wrapper
}  // namespace proxy
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_PROXY_TESTS_MOCKS_PROXY_COM_WRAPPER_MOCK_H_
