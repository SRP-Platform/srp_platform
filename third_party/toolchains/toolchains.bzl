load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

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

    # Raspberry Pi Zero 2 W (Alpine Linux, musl) - toolchain Bootlin, gcc 14.3
    if "aarch64-linux-musl" not in native.existing_rules():
        http_archive(
            name = "aarch64-linux-musl",
            build_file = Label("@srp_platform//third_party/toolchains:aarch64-linux-musl.BUILD"),
            url = "https://toolchains.bootlin.com/downloads/releases/toolchains/aarch64/tarballs/aarch64--musl--stable-2025.08-1.tar.xz",
            sha256 = "defba831ffa1175236f137069333e21ed46d4d19feb5080a90cf248b6fc2cb08",
            strip_prefix = "aarch64--musl--stable-2025.08-1",
        )
