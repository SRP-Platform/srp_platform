/**
 * @file msg_type.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-22
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef PLATFORM_COM_MSG_TYPE_H_
#define PLATFORM_COM_MSG_TYPE_H_

#include <cstdint>
#include <vector>

#include "core/data/type_converter.h"

namespace platform {
namespace com {
enum class IpcMsgType : uint8_t {
  // Common fo IPC and for internal
  kRequest = 0x01U,
  kResponse = 0x02U,
  kEvent = 0x03U,
  kSubscribe = 0x04U,
  kSubscribeAck = 0x05,
  kStopSubscribe = 0x06U,
  kRequestNoReturn = 0x07U,
  kRequestNoReturnAck = 0x08U,
  kRequestAck = 0x09U,
  kFind = 0x10U,
  kOffer = 0x11U,
  // Internal only
  kObjInit = 0x50U,  // Send by constructor in Event and Filed on Skeleto side
  // Error Stauts (Common)
  kError = 0xFFU,
};
struct IpcMsg {
  IpcMsgType msg_type_;
  uint16_t service_id_;
  uint16_t instance_id_;
  uint16_t method_id_;
  uint32_t app_id_;
  uint16_t session_id_;
  uint32_t length_;
  std::vector<uint8_t> payload_;

  bool operator==(const IpcMsg& other) const {
    bool flag = (msg_type_ == other.msg_type_);
    flag &= (service_id_ == other.service_id_);
    flag &= (instance_id_ == other.instance_id_);
    flag &= (method_id_ == other.method_id_);
    flag &= (app_id_ == other.app_id_);
    flag &= (session_id_ == other.session_id_);
    flag &= (length_ == other.length_);
    flag &= (payload_ == other.payload_);
    return flag;
  }
};

}  // namespace com
}  // namespace platform

namespace srp {
namespace data {

template <>
struct Convert<platform::com::IpcMsg> {
  static std::optional<platform::com::IpcMsg> Conv(
      const std::vector<std::uint8_t>& in) {
    platform::com::IpcMsg res{};
    if (in.size() < 16) {
      return std::nullopt;
    }
    {
      const auto tem_v = srp::data::Convert<std::uint8_t>::Conv(
          std::vector<uint8_t>{in.begin() + 0, in.begin() + 1});
      if (!tem_v.has_value()) {
        return std::nullopt;
      }
      res.msg_type_ = platform::com::IpcMsgType{tem_v.value()};
    }
    {
      const auto tem_v = srp::data::Convert<std::uint16_t>::Conv(
          std::vector<uint8_t>{in.begin() + 1, in.begin() + 3});
      if (!tem_v.has_value()) {
        return std::nullopt;
      }
      res.service_id_ = tem_v.value();
    }
    {
      const auto tem_v = srp::data::Convert<std::uint16_t>::Conv(
          std::vector<uint8_t>{in.begin() + 3, in.begin() + 5});
      if (!tem_v.has_value()) {
        return std::nullopt;
      }
      res.instance_id_ = tem_v.value();
    }
    {
      const auto tem_v = srp::data::Convert<std::uint16_t>::Conv(
          std::vector<uint8_t>{in.begin() + 5, in.begin() + 7});
      if (!tem_v.has_value()) {
        return std::nullopt;
      }
      res.method_id_ = tem_v.value();
    }
    {
      const auto tem_v = srp::data::Convert<std::uint32_t>::Conv(
          std::vector<uint8_t>{in.begin() + 7, in.begin() + 11});
      if (!tem_v.has_value()) {
        return std::nullopt;
      }
      res.app_id_ = tem_v.value();
    }
    {
      const auto tem_v = srp::data::Convert<std::uint16_t>::Conv(
          std::vector<uint8_t>{in.begin() + 11, in.begin() + 13});
      if (!tem_v.has_value()) {
        return std::nullopt;
      }
      res.session_id_ = tem_v.value();
    }
    {
      const auto tem_v = srp::data::Convert<std::uint32_t>::Conv(
          std::vector<uint8_t>{in.begin() + 13, in.begin() + 17});
      if (!tem_v.has_value()) {
        return std::nullopt;
      }
      res.length_ = tem_v.value();
    }
    res.payload_ =
        std::vector<uint8_t>{in.begin() + 17, in.begin() + 17 + res.length_};
    return res;
  }
};
template <>
struct Convert2Vector<platform::com::IpcMsg> {
  static std::vector<uint8_t> Conv(const platform::com::IpcMsg& in) {
    std::vector<uint8_t> out{};
    {
      const auto temp_r_v = srp::data::Convert2Vector<std::uint8_t>::Conv(
          static_cast<uint8_t>(in.msg_type_));
      out.insert(out.end(), temp_r_v.begin(), temp_r_v.end());
    }
    {
      const auto temp_r_v =
          srp::data::Convert2Vector<std::uint16_t>::Conv(in.service_id_);
      out.insert(out.end(), temp_r_v.begin(), temp_r_v.end());
    }
    {
      const auto temp_r_v =
          srp::data::Convert2Vector<std::uint16_t>::Conv(in.instance_id_);
      out.insert(out.end(), temp_r_v.begin(), temp_r_v.end());
    }
    {
      const auto temp_r_v =
          srp::data::Convert2Vector<std::uint16_t>::Conv(in.method_id_);
      out.insert(out.end(), temp_r_v.begin(), temp_r_v.end());
    }
    {
      const auto temp_r_v =
          srp::data::Convert2Vector<std::uint32_t>::Conv(in.app_id_);
      out.insert(out.end(), temp_r_v.begin(), temp_r_v.end());
    }
    {
      const auto temp_r_v =
          srp::data::Convert2Vector<std::uint16_t>::Conv(in.session_id_);
      out.insert(out.end(), temp_r_v.begin(), temp_r_v.end());
    }
    {
      const auto temp_r_v =
          srp::data::Convert2Vector<std::uint32_t>::Conv(in.length_);
      out.insert(out.end(), temp_r_v.begin(), temp_r_v.end());
    }
    out.insert(out.end(), in.payload_.begin(), in.payload_.end());
    return out;
  }
};
}  // namespace data
}  // namespace srp

#endif  // PLATFORM_COM_MSG_TYPE_H_
