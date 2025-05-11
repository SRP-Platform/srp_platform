
cc_binary(
    name = "someip_demon",
    srcs = [
        "main.cc",
        "someip_demon.cc",
        "someip_demon.h",
    ],
    visibility = ["//apps/common/someip_demon:__subpackages__"],
    deps = [
        "@srp_platform//apps/common/someip_demon/model:runtime_env",
        "@srp_platform//platform/exec:adaptive_application_lib",
        "@srp_platform//core/common:condition_lib",
    ],
)
