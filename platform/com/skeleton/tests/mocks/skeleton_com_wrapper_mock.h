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

#ifndef PLATFORM_COM_SKELETON_TESTS_MOCKS_SKELETON_COM_WRAPPER_MOCK_H_
#define PLATFORM_COM_SKELETON_TESTS_MOCKS_SKELETON_COM_WRAPPER_MOCK_H_
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "platform/com/msg_type.h"
#include "platform/com/skeleton/i_skeleton_com_wrapper.h"

namespace platform {
namespace com {
namespace skeleton {
namespace wrapper {
class SkeletonComWrapperMock : public ISkeletonComWrapper {
 public:
  MOCK_METHOD(void, ProceedFrame, (const platform::com::IpcMsg&),
              (noexcept, override));
  MOCK_METHOD(bool, TransmitFrame, (const platform::com::IpcMsg&),
              (noexcept, override));
  MOCK_METHOD(void, SetProceedFrameCallback,
              (const ISkeletonComWrapper::ProceedFrameCallback&&),
              (noexcept, override));
  virtual ~SkeletonComWrapperMock() = default;
};
}  // namespace wrapper
}  // namespace skeleton
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_SKELETON_TESTS_MOCKS_SKELETON_COM_WRAPPER_MOCK_H_
