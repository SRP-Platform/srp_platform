/**
 * Copyright (c) 2024
 *
 * @file byte_transport_contract_test.h
 * @brief TESTONLY: wspólny zestaw testów kontraktu IByteTransport.
 *
 * Użycie (także w repozytoriach z własnym transportem; nazwa zestawu w makrze musi być
 * niekwalifikowana, stąd `using`):
 * @code
 *   using ara::com::mavlink::testing::ByteTransportContractTest;
 *   INSTANTIATE_TEST_SUITE_P(
 *       MyTransport, ByteTransportContractTest,
 *       ::testing::Values(ara::com::mavlink::testing::ContractParam{
 *           "my", [] { return MakeHarness(); }}),
 *       ara::com::mavlink::testing::ContractParamName);
 * @endcode
 *
 * Harness dostarcza transport oraz "drugą stronę przewodu".
 */
#ifndef ARA_COM_MAVLINK_TESTING_BYTE_TRANSPORT_CONTRACT_TEST_H_
#define ARA_COM_MAVLINK_TESTING_BYTE_TRANSPORT_CONTRACT_TEST_H_

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ara/com/mavlink/i_byte_transport.h"

namespace ara {
namespace com {
namespace mavlink {
namespace testing {

struct ByteTransportHarness {
  /// Testowany transport (jeszcze nie otwarty).
  std::shared_ptr<IByteTransport> transport;
  /// Bajty pojawiają się po stronie Read() transportu (wołane po Open()).
  std::function<void(const std::vector<std::uint8_t>&)> peer_write;
  /// Bajty zapisane przez transport; zwraca do `want` bajtów (>= 1, jeśli
  /// cokolwiek dotarło w `timeout_ms`), pusty wektor = nic.
  std::function<std::vector<std::uint8_t>(std::size_t want,
                                          std::uint32_t timeout_ms)>
      peer_read;
  /// Ile bajtów na pewno przepełni bufory przewodu (0 = pomiń testy zapisu
  /// blokującego/częściowego).
  std::size_t overflow_bytes{0};
};

using HarnessFactory = std::function<ByteTransportHarness()>;

struct ContractParam {
  std::string name;
  HarnessFactory factory;
};

class ByteTransportContractTest
    : public ::testing::TestWithParam<ContractParam> {
 protected:
  void SetUp() override { h_ = GetParam().factory(); }
  void TearDown() override {
    if (h_.transport) {
      h_.transport->Close();
    }
  }
  ByteTransportHarness h_;
};

std::string ContractParamName(
    const ::testing::TestParamInfo<ContractParam>& info);

}  // namespace testing
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_TESTING_BYTE_TRANSPORT_CONTRACT_TEST_H_
