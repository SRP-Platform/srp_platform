# Platform service of the MavlinkDaemon: radio measurements (RSSI, noise floor, SNR) and counters
# per link. Offered by the daemon on `ipc` (instance 1); an application takes it with
#   {"name": "srp.platform.mavlink.MavlinkLinkService as LinkRPort", "on": "ipc", "instance": 1}
# and lists this filegroup in its model sources (see platform/common/mavlink_daemon/README.md).
filegroup(
    name = "mavlink_link_service",
    srcs = [
        "link_name_data.json",
        "link_stats.json",
        "mavlink_link_service.json",
    ],
    visibility = ["//visibility:public"],
)

# Pack / unpack of `link_name_lo` / `link_name_hi` (LinkStatsData).
cc_library(
    name = "link_name",
    hdrs = ["link_name.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "test_link_name",
    srcs = ["test_link_name.cc"],
    deps = [
        ":link_name",
        "@com_google_googletest//:gtest_main",
    ],
)
