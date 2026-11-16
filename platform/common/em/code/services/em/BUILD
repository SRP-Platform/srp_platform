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
    visibility = [
        "@srp_platform//platform/common/em/code:__subpackages__",
        "@srp_platform//tests:__subpackages__",
    ],
    deps = [
        ":em_db",
        "@srp_platform//ara/com:com_controller",
        "@srp_platform//ara/exec/em:em_com_data",
        "@srp_platform//ara/exec/em:execution_client_lib",
        "@srp_platform//ara/log",
        "@srp_platform//bindings/common/socket:ipc_socket",
        "@srp_platform//core/common:condition_lib",
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
    deps = [
        "@srp_platform//ara/exec/em:execution_client_lib",
        "@srp_platform//ara/log",
    ],
    visibility = [
        "@srp_platform//mw/em/code/services/exec:__subpackages__",
        "@srp_platform//platform/common/em/code:__subpackages__",
        "@srp_platform//tests:__subpackages__",
    ],
)
