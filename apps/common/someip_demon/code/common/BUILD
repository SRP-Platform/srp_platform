cc_library(
    name = "udp_lib",
    srcs = [
        "udp_multicast_socket.cc",
        "udp_socket.cc",
    ],
    hdrs = [
        "udp_multicast_socket.h",
        "udp_socket.h",
    ],
    data = [],
    visibility = [
        "@srp_platform//apps/common/logger/code/application:__subpackages__",
        "@srp_platform//apps/common/someip_demon/code:__subpackages__",
    ],
    deps = [
        "@srp_platform//platform/log",
        "@srp_platform//core/common:common_types",
    ],
)

# cc_library(
#     name = "controller",
#     srcs = ["network_controller.cc"],
#     hdrs = [
#         "i_controller.h",
#         "i_network_controller.h",
#         "network_controller.h",
#     ],
#     visibility = ["@srp_platform//platform/common/someip_demon/code:__subpackages__"],
#     deps = [
#         "@srp_platform//platform/com/someip:someip_data",
#         "@srp_platform//platform/core:Result",
#         "@srp_platform//platform/log",
#     ],
# )
