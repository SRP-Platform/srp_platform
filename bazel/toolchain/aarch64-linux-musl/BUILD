package(default_visibility = ["//visibility:public"])

load(":cc_toolchain_config.bzl", "cc_toolchain_config")

filegroup(name = "empty")

filegroup(
    name = "wrappers",
    srcs = glob(["wrappers/**"]),
)

filegroup(
    name = "all_files",
    srcs = [
        ":wrappers",
        "@aarch64-linux-musl//:toolchain",
    ],
)

cc_toolchain_config(name = "aarch64_musl_toolchain_config")

cc_toolchain(
    name = "aarch64_musl_toolchain",
    all_files = ":all_files",
    ar_files = ":all_files",
    compiler_files = ":all_files",
    dwp_files = ":empty",
    linker_files = ":all_files",
    objcopy_files = ":empty",
    strip_files = ":empty",
    toolchain_config = ":aarch64_musl_toolchain_config",
    toolchain_identifier = "aarch64-musl-toolchain",
)

# Raspberry Pi Zero 2 W z Alpine Linux (musl) - binarki linkowane dynamicznie
# z bibliotekami systemu (libstdc++, libgcc z pakietow Alpine).
toolchain(
    name = "aarch64_linux_musl_toolchain",
    exec_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
        "@srp_platform//bazel/platforms:board_rpi_zero2w",
    ],
    toolchain = ":aarch64_musl_toolchain",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)
