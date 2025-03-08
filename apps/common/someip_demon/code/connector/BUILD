cc_library(
    name = "connector",
    srcs = ["service_connector.cc"],
    hdrs = ["service_connector.h"],
    visibility = ["@srp_platform//platform/common/someip_demon/code:__subpackages__"],
    deps = [
        "@srp_platform//bindings/common/socket:ipc_socket",
        "@srp_platform//platform/com/someip:someip_data",
        "@srp_platform//platform/log",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//platform/common/someip_demon/code/common:udp_lib",
        "@srp_platform//platform/common/someip_demon/code/connector:internal",
        "@srp_platform//platform/common/someip_demon/code/db",
        "@srp_platform//platform/common/someip_demon/code/sd:sd_controller",
    ],
)

cc_library(
    name = "internal",
    srcs = [
        "common_connector.cc",
        "proxy_connector.cc",
        "sd_connector.cc",
        "skeleton_connector.cc",
    ],
    hdrs = [
        "common_connector.h",
        "i_connector.h",
        "i_db.h",
        "proxy_connector.h",
        "sd_connector.h",
        "skeleton_connector.h",
    ],
    visibility = ["@srp_platform//platform/common/someip_demon/code/connector:__pkg__"],
    deps = [
        "@srp_platform//bindings/common/socket:stream_socket_ipc",
        "@srp_platform//bindings/common/socket:ipc_socket",
        "@srp_platform//platform/com/someip:someip_data",
        "@srp_platform//platform/log",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//platform/common/someip_demon/code/common:udp_lib",
        "@srp_platform//platform/common/someip_demon/code/db",
    ],
)
