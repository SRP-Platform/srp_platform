load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

URL_TOOLCHAIN = "https://github.com/ltekieli/devboards-toolchains/releases/download/v2024.11.01/"
URL_SYSROOT = "https://github.com/ltekieli/buildroot/releases/download/v2021.12.01/"
SYSROOT_VERSION = "0.1"
def toolchains():
    if "arm-cortex_a8-linux-gnueabihf" not in native.existing_rules():
        http_archive(
            name = "arm-cortex_a8-linux-gnueabihf",
            build_file = Label("@srp_platform//third_party/toolchains:arm-cortex_a8-linux-gnueabihf.BUILD"),
            url = "https://github.com/Simba-Avionic/fc_yocto/releases/download/v8.0/arm-cortex_a8-linux-gnueabihf.tar.gz",
            sha256 = "f2b74022f80ed02253b302e993907b57b9d13107be55d08bdaaae637a6ebf749",
        )

    if "arm-cortex_a8-linux-gnueabihf-sysroot" not in native.existing_rules():
        http_archive(
            name = "arm-cortex_a8-linux-gnueabihf-sysroot",
            build_file = Label("@srp_platform//third_party/toolchains:arm-cortex_a8-linux-gnueabihf-sysroot.BUILD"),
            url = "https://github.com/Simba-Avionic/fc_yocto/releases/download/v8.0/beaglebone.tar.gz",
            sha256 = "324e5db4daea321fba5fc283a1375340570e3857ee70395ceba12e67c299cee5",
        )
    if "aarch64-rpi3-linux-gnu" not in native.existing_rules():
        http_archive(
            name = "aarch64-rpi3-linux-gnu",
            build_file = Label("//third_party/toolchains:aarch64-rpi3-linux-gnu.BUILD"),
            url = "https://github.com/ltekieli/devboards-toolchains/releases/download/v2023.04.01/aarch64-rpi3-linux-gnu.tar.gz",
            sha256 = "83925df5df8b66fdf41c9c0c5b4282d77dacce81a2d02d3af02dbde6a1d12c7f",
        )
    if "aarch64-rpi3-linux-gnu-sysroot" not in native.existing_rules():
        http_archive(
            name = "aarch64-rpi3-linux-gnu-sysroot",
            build_file = Label("//third_party/toolchains:aarch64-rpi3-linux-gnu-sysroot.BUILD"),
            url = "https://github.com/Mateusz-Krajewski/test_sysroot/archive/refs/tags/" + SYSROOT_VERSION + ".zip",
            strip_prefix = "test_sysroot-" + SYSROOT_VERSION,
            sha256 = "c15deeea5de6590620bb2113ed5d96d742bdae0c80cf745ade9c6393c107919d",
        )
