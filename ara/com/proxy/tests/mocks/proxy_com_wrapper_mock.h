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

#ifndef ARA_COM_PROXY_TESTS_MOCKS_PROXY_COM_WRAPPER_MOCKS_H_
#define ARA_COM_PROXY_TESTS_MOCKS_PROXY_COM_WRAPPER_MOCKS_H_
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ara/com/msg_type.h"
#include "ara/com/proxy/i_proxy_com_wrapper.h"

namespace ara {
namespace com {
namespace proxy {
namespace wrapper {
class ProxyComWrapperMock : public IProxyComWrapper {
 public:
  MOCK_METHOD(void, ProceedFrame, (const ara::com::IpcMsg&),
              (noexcept, override));
  MOCK_METHOD(void, TransmitFrame, (const ara::com::IpcMsg&),
              (noexcept, override));
  MOCK_METHOD(void, SetProceedFrameCallback, (const IProxyComWrapper::ProceedFrameCallback&&),
              (noexcept, override));
  virtual ~ProxyComWrapperMock() = default;
};
}  // namespace wrapper
}  // namespace proxy
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_PROXY_TESTS_MOCKS_PROXY_COM_WRAPPER_MOCKS_H_
