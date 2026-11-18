"""Bazel rule: application models of a CPU -> MAVLink dialect artifacts.

The dialect is per CPU: `apps` are the models of ALL applications with `on: mavlink` of one
CPU (one MavlinkDaemon); a msg_id collision between applications is a generation error.
`daemon_config` (optional) is the CPU's mavlink_daemon_config.json: it enables checks of link
names, versions and signing mode and names every link in the matrix.

Outputs: <dialect>.xml, <dialect>_links.md, <dialect>.lua (Wireshark) and <dialect>.py (pymavlink).
"""

def _ara_mavlink_dialect_impl(ctx):
    d = ctx.attr.dialect
    outs = [ctx.actions.declare_file(d + ext) for ext in (".xml", "_links.md", ".lua", ".py")]
    args = ctx.actions.args()
    args.add(outs[0].dirname)
    args.add("--dialect", d)
    inputs = ctx.files.apps + ctx.files.data
    if ctx.file.daemon_config:
        args.add("--daemon-config", ctx.file.daemon_config)
        inputs = inputs + [ctx.file.daemon_config]
    args.add_all(ctx.files.apps)
    ctx.actions.run(
        inputs = inputs,
        outputs = outs,
        arguments = [args],
        executable = ctx.executable._gen,
        mnemonic = "MavlinkDialect",
        progress_message = "Generating MAVLink dialect %s" % d,
    )
    return [DefaultInfo(files = depset(outs))]

ara_mavlink_dialect = rule(
    implementation = _ara_mavlink_dialect_impl,
    attrs = {
        "apps": attr.label_list(allow_files = [".json"], mandatory = True),
        "daemon_config": attr.label(allow_single_file = [".json"]),
        "data": attr.label_list(allow_files = [".json"]),
        "dialect": attr.string(mandatory = True),
        "_gen": attr.label(
            executable = True,
            cfg = "exec",
            default = Label("@srp_platform//tools/model_generator/ara/mavlink:mavlink_gen"),
        ),
    },
)
