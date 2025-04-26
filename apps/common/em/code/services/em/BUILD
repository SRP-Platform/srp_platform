cc_library(
    name = "em_service",
    srcs = [
        "em_service.cc",
        "json_parser.cc",
        "aplication_status_did.cc",
    ],
    hdrs = [
        "em_service.h",
        "json_parser.h",
        "aplication_status_did.h"
    ],
    visibility = ["@srp_platform//apps/common/em/code:__subpackages__"],
    deps = [
        ":em_db",
        "@srp_platform//platform/log",
        "@srp_platform//core/json:simba_json",
        "@srp_platform//platform/diag:uds_lib",
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
    deps = [
        "@srp_platform//platform/exec/em:execution_client_lib",
        "@srp_platform//platform/exec/em:em_com_data",
    ],
    visibility = [
        "@srp_platform//mw/em/code/services/exec:__subpackages__",
    ],
)
