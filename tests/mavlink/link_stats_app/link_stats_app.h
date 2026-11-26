/**
 * @file link_stats_app.h
 * @copyright Copyright (c) 2024
 * @brief Test client of the MavlinkLinkService over the real `ipc` (`require`): prints every
 * `LinkStats` event and calls `GetLinkStats` for a known and an unknown link.
 */
#ifndef TESTS_MAVLINK_LINK_STATS_APP_LINK_STATS_APP_H_
#define TESTS_MAVLINK_LINK_STATS_APP_LINK_STATS_APP_H_

#include <map>
#include <stop_token>  // NOLINT

#include "ara/exec/adaptive_application.h"

namespace srp {
namespace test {

class LinkStatsApp final : public ara::exec::AdaptiveApplication {
 private:
  int Initialize(const std::map<ara::core::StringView, ara::core::StringView> parms) override;
  int Run(const std::stop_token& token) override;
};

}  // namespace test
}  // namespace srp

#endif  // TESTS_MAVLINK_LINK_STATS_APP_LINK_STATS_APP_H_
