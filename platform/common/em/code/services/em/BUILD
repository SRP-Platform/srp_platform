cc_library(
    name = "em_service",
    srcs = [
        "em_service.cc",
        "json_parser.cc",
    ],
    hdrs = [
        "em_service.h",
        "json_parser.h",
    ],
    visibility = ["@srp_platform//platform/common/em/code:__subpackages__"],
    deps = [
        ":em_db",
        "@srp_platform//ara/log",
        "@srp_platform//core/json:simba_json",
        "@srp_platform//ara/exec/em:execution_client_lib",
    ],
)

cc_library(
    name = "em_db",
    srcs = ["app_db.cc"],
    hdrs = [
        "app_config.h",
        "app_db.h",
        "i_app_db.h",
    ],
    visibility = [
        "@srp_platform//mw/em/code/services/exec:__subpackages__",
    ],
    deps = [
        "@srp_platform//ara/exec/em:execution_client_lib",
    ]
)
