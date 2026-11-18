"""Reguła bazela dla pluginów driverów radia MAVLink (ładowanych przez MavlinkDaemon dlopen).

Użycie (w repo srp, obok kodu drivera):

    load("@srp_platform//ara/com/mavlink:mavlink_driver_plugin.bzl", "mavlink_driver_plugin")

    mavlink_driver_plugin(
        name = "ebyte_e220",          # powstaje libebyte_e220.so (+ filegroup :ebyte_e220)
        srcs = ["e220_plugin.cc"],    # zawiera SRP_MAVLINK_DRIVER_PLUGIN(RegisterDrivers)
        deps = [":e220_driver"],
    )

Plik `.so` trafia do katalogu driverów z configu daemona (`driver_dir`).
"""

def mavlink_driver_plugin(name, srcs, deps = [], hdrs = [], copts = [], linkopts = [], testonly = False, visibility = None, **kwargs):
    """Biblioteka dynamiczna z pluginem driverów (symbole z makra SRP_MAVLINK_DRIVER_PLUGIN).

    Args:
      name: nazwa pluginu; plik wynikowy to lib<name>.so, filegroup <name> go zawiera.
      srcs: źródła (jedno z nich używa makra SRP_MAVLINK_DRIVER_PLUGIN).
      deps: zależności (statycznie wkompilowane w .so); API drivera dodawane automatycznie.
      hdrs: nagłówki własne.
      copts: dodatkowe opcje kompilacji.
      linkopts: dodatkowe opcje linkera.
      testonly: plugin tylko do testów.
      visibility: widoczność.
      **kwargs: przekazywane do cc_binary.
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
