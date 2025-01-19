cc_library(
    name = "sd_controller",
    srcs = ["sd_controller.cc"],
    hdrs = ["sd_controller.h"],
    visibility = ["@srp_platform//platform/common/someip_demon/code:__subpackages__"],
    deps = [
        "@srp_platform//platform/common/someip_demon/code/common:controller",
        "@srp_platform//platform/common/someip_demon/code/common/com:interface",
        "@srp_platform//platform/common/someip_demon/code/db",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//ara/com/someip:someip_data",
        "@srp_platform//ara/log",
    ],
)
