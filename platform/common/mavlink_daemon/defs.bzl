"""Bazel rule that builds a MavlinkDaemon binary with statically linked radio drivers.

Radio drivers are not loaded at run time: they are linked into the daemon binary and register
themselves from a static initializer (`SRP_MAVLINK_REGISTER_DRIVER`, ara/com/mavlink/radio_driver_registry.h).
The integrator (the srp repository) writes only a BUILD entry, no C++:

    load("@srp_platform//platform/common/mavlink_daemon:defs.bzl", "mavlink_daemon_binary")

    mavlink_daemon_binary(
        name = "mavlink_daemon",
        drivers = ["//core/radio/e220:e220_driver"],   # cc_library(..., alwayslink = True)
    )

A driver library has a .cc file with `SRP_MAVLINK_REGISTER_DRIVER("id", factory)` and MUST set
`alwayslink = True` (otherwise the linker drops the object file and the driver is missing).
`serial_radio` is always linked in. `links.<name>.driver` of the daemon config selects a driver by id.
"""

_APP_MAIN = """#include "ara/exec/adaptive_lifecycle.h"
#include "platform/common/mavlink_daemon/code/application/mavlink_daemon.h"

int main(int argc, char const* argv[]) {
  return ara::exec::RunAdaptiveLifecycle<srp::mavlink_daemon::MavlinkDaemonApplication>(argc, argv);
}
"""

_HOST_MAIN = """#include "platform/common/mavlink_daemon/code/application/daemon_host.h"

int main(int argc, char** argv) { return srp::mavlink_daemon::RunDaemonHost(argc, argv); }
"""

def mavlink_daemon_binary(name, drivers = [], host = False, testonly = False, visibility = None, **kwargs):
    """The MavlinkDaemon binary with the given radio drivers linked in.

    Args:
      name: name of the cc_binary.
      drivers: cc_library targets with `SRP_MAVLINK_REGISTER_DRIVER` (each `alwayslink = True`);
        `serial_radio` is added automatically.
      host: false (default) = the platform application under Execution Management (what runs on
        the target). true = the same daemon core without Execution Management, controlled through
        stdin/stdout JSON lines (`daemon_host`); for tests on a PC (usually with testonly = True).
      testonly: the binary is for tests only.
      visibility: visibility.
      **kwargs: passed to cc_binary.
    """
    main = _HOST_MAIN if host else _APP_MAIN
    native.genrule(
        name = name + "_main",
        outs = [name + "_main.cc"],
        cmd = "cat > $@ <<'MAVLINK_DAEMON_MAIN_EOF'\n" + main + "MAVLINK_DAEMON_MAIN_EOF\n",
        testonly = testonly,
    )
    native.cc_binary(
        name = name,
        srcs = [":" + name + "_main"],
        deps = drivers + [
            "@srp_platform//ara/com/mavlink:serial_radio",
            "@srp_platform//platform/common/mavlink_daemon/code/application:" + ("daemon_host_lib" if host else "daemon_lib"),
        ],
        testonly = testonly,
        visibility = visibility,
        **kwargs
    )
