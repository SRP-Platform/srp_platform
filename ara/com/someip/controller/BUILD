cc_library(
    name = "controller",
    srcs = ["someip_controller.cc"],
    hdrs = [
        "i_someip_controller.h",
        "someip_controller.h",
    ],
    data = [],
    visibility = ["@srp_platform//ara/com:__subpackages__"],
    deps = [
        "@srp_platform//ara/com:com_controller",
        "@srp_platform//ara/com:com_log",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/com/someip:someip_data"
    ],
)
