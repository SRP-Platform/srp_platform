def register_all_toolchains():
    native.register_toolchains(
        "@srp_platform//bazel/toolchain/arm-cortex_a8-linux-gnueabihf:armv7_linux_toolchain",
        "@srp_platform//bazel/toolchain/aarch64-rpi3-linux-gnu:aarch64_linux_toolchain",
    )
