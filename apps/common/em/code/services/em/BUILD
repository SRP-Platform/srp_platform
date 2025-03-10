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
    visibility = ["@srp_platform//apps/common/em/code:__subpackages__"],
    deps = [
        ":em_db",
        "@srp_platform//platform/log",
        "@srp_platform//core/json:simba_json",
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
        "//platform/exec/em:execution_client_lib",
    ],
)
