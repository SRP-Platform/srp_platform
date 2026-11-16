def register_all_toolchains():
    native.register_toolchains(
        "@srp_platform//bazel/toolchain/arm-cortex_a8-linux-gnueabihf:armv7_linux_toolchain",
    )
    native.register_toolchains(
        "@srp_platform//bazel/toolchain/arm-cortex_a8-linux-gnueabihf:armv7_linux_toolchain_macos",
    )
    native.register_toolchains(
        "@srp_platform//bazel/toolchain/aarch64-linux-musl:aarch64_linux_musl_toolchain",
    )
