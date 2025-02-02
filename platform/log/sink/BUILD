cc_library(
    name = "sink_lib",
    srcs = [
        "console_log_sink.cpp",
        "file_log_sink.cpp",
        "log_sink.cpp",
        "remote_log_sink.cpp",
    ],
    hdrs = [
        "console_log_sink.h",
        "file_log_sink.h",
        "log_sink.h",
        "remote_log_sink.h",
    ],
    visibility = ["@srp_platform//platform/log:__subpackages__"],
    deps = [
        "@srp_platform//platform/log:LogStream",
        "@srp_platform//bindings/common/socket:socket_ipc",
        "@srp_platform//core/common:wait_queue",
    ],
)
