/**
 * @file event.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-12
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef ARA_COM_PROXY_EVENT_H_
#define ARA_COM_PROXY_EVENT_H_

#include "ara/com/proxy/event_packet_interpreter.h"
#include "ara/com/types.h"
#include "ara/core/result.h"

namespace ara {
namespace com {
namespace proxy {
namespace interpreter {
class ProxyPacketInterpreter;
}  // namespace interpreter

template <typename DataType>
class Event : public interpreter::EventPacketInterpreter<DataType> {
 private:
  ara::com::SubscriptionState s_state_{
      ara::com::SubscriptionState::kNotSubscribed};
  ara::com::SubscriptionStateChangeHandler s_handler_;
  ara::com::EventReceiveHandler r_handler_;
  DataType* sample_{nullptr};

  void SetNewValue(const DataType& new_value) noexcept override {
    if (sample_ != nullptr) {
      delete sample_;
    }
    sample_ = new DataType(new_value);
    if (r_handler_) {
      r_handler_();
    }
  }

  void SetNewSubscriptionState(
      const ara::com::SubscriptionState& new_state_) noexcept override {
    if (new_state_ != s_state_) {
      s_state_ = new_state_;
      if (s_handler_) {
        s_handler_();
      }
    }
  }

 public:
  explicit Event(const std::string name,
                 interpreter::ProxyPacketInterpreter& handler) noexcept
      : interpreter::EventPacketInterpreter<DataType>(name, handler) {}

  explicit Event(Event&&) = delete;
  explicit Event(Event&) = delete;
  Event operator=(Event&) = delete;
  Event operator=(Event&&) = delete;

  ~Event() noexcept {
    if (sample_ != nullptr) {
      delete sample_;
    }
  }

  void Unsubscribe() noexcept {}

  ara::core::Result<void> Subscribe(
      ara::com::SubscriptionStateChangeHandler&& handler,
      size_t maxSampleCount) noexcept {
    return {};
  }

  ara::com::SubscriptionState GetSubscriptionState() const {
    return this->s_state_;
  }

  ara::core::Result<void> SetReceiveHandler(
      ara::com::EventReceiveHandler&& handler) noexcept {
    return {};
  }

  ara::core::Result<size_t> GetNewSamples(
      GetNewSampleHandler<DataType*>&& handler,
      size_t maxNumberOfSamples) noexcept {
    return {0U};
  }
};
}  // namespace proxy
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_PROXY_EVENT_H_
