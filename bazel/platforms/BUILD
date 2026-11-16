package(default_visibility = ["//visibility:public"])

# Plytka docelowa. Pozwala wybierac kod/konfiguracje zalezne od sprzetu:
#   select({"@srp_platform//bazel/platforms:is_rpi_zero2w": [...], ...})
constraint_setting(name = "board")

constraint_value(
    name = "board_bbb",
    constraint_setting = ":board",
)

constraint_value(
    name = "board_rpi_zero2w",
    constraint_setting = ":board",
)

# BeagleBone Black: armv7, glibc (Yocto) - toolchain arm-cortex_a8-linux-gnueabihf
platform(
    name = "bbb",
    constraint_values = [
        "@platforms//cpu:armv7",
        "@platforms//os:linux",
        ":board_bbb",
    ],
)

# Raspberry Pi Zero 2 W: aarch64, musl (Alpine) - toolchain aarch64-linux-musl
platform(
    name = "rpi_zero2w",
    constraint_values = [
        "@platforms//cpu:aarch64",
        "@platforms//os:linux",
        ":board_rpi_zero2w",
    ],
)

config_setting(
    name = "is_bbb",
    constraint_values = [":board_bbb"],
)

config_setting(
    name = "is_rpi_zero2w",
    constraint_values = [":board_rpi_zero2w"],
)
