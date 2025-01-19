licenses(["notice"])  # Apache 2

cc_library(
    name = "libdoipserver",
    srcs = glob(["src/*.cpp"]),
    hdrs = glob(["include/*.h"]),
    includes = [
        "./src",
        "./include",
    ],
    visibility = ["//visibility:public"],
    deps = ["@srp_platform//libdoip/libdoipcommon"],
)

