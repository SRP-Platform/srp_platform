cc_library(
    name = "shm_lib",
    hdrs = [
        "shm_proxy.h",
        "shm_skeleton.h",
    ],
    visibility = ["@srp_platform//visibility:public"],
    deps = ["@srp_platform//ara/com:com_error_domain"],
)

cc_library(
    name = "shm_buffor_lib",
    hdrs = [
        "shm_buffor_proxy.h",
        "shm_buffor_skeleton.h",
    ],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:condition_variable",
    ],
)
