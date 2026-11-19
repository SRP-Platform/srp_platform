"""Bazel rule for MAVLink radio driver plugins (loaded by the MavlinkDaemon with dlopen).

Usage (in the srp repo, next to the driver code):

    load("@srp_platform//ara/com/mavlink:mavlink_driver_plugin.bzl", "mavlink_driver_plugin")

    mavlink_driver_plugin(
        name = "ebyte_e220",          # produces libebyte_e220.so (+ filegroup :ebyte_e220)
        srcs = ["e220_plugin.cc"],    # contains SRP_MAVLINK_DRIVER_PLUGIN(RegisterDrivers)
        deps = [":e220_driver"],
    )

The `.so` file goes to the driver directory of the daemon config (`driver_dir`).
"""

def mavlink_driver_plugin(name, srcs, deps = [], hdrs = [], copts = [], linkopts = [], testonly = False, visibility = None, **kwargs):
    """Shared library with a driver plugin (symbols from the SRP_MAVLINK_DRIVER_PLUGIN macro).

    Args:
      name: plugin name; the output file is lib<name>.so, the filegroup <name> contains it.
      srcs: sources (one of them uses the SRP_MAVLINK_DRIVER_PLUGIN macro).
      deps: dependencies (statically linked into the .so); the driver API is added automatically.
      hdrs: own headers.
      copts: extra compile options.
      linkopts: extra linker options.
      testonly: the plugin is for tests only.
      visibility: visibility.
      **kwargs: passed to cc_binary.
    """
    native.cc_binary(
        name = "lib" + name + ".so",
        srcs = srcs + hdrs,
        deps = deps + ["@srp_platform//ara/com/mavlink:radio_driver"],
        copts = copts + ["-fvisibility=hidden"],
        linkopts = linkopts,
        linkshared = True,
        linkstatic = True,
        testonly = testonly,
        visibility = visibility,
        **kwargs
    )
    native.filegroup(
        name = name,
        srcs = [":lib" + name + ".so"],
        testonly = testonly,
        visibility = visibility,
    )
