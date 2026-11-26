"""Bazel rule: application models of a CPU -> MAVLink dialect artifacts.

The dialect is per CPU: `apps` are the models of ALL applications with `on: mavlink` of one
CPU (one MavlinkDaemon); a msg_id collision between applications is a generation error.
`daemon_config` (optional) is the CPU's mavlink_daemon_config.json: it enables checks of link
names, versions and signing mode and names every link in the matrix.

Outputs: <dialect>.xml, <dialect>_links.md, <dialect>.lua (Wireshark), <dialect>.py (pymavlink),
<dialect>_monitor.py (the ground monitor, runtime: tools/mavlink/mavlink_monitor.py) and
<dialect>_monitor.json (its metadata, input of ara_mavlink_system).

`ara_mavlink_system` merges the dialects of several CPUs (one radio daemon each) into one dialect
for the ground station; see the rule below.

Both are macros: next to the rule they expose the generated files as the filegroups
`<dialect>_monitor.py`, `<name>_dialect_py` and `<name>_monitor_json`.
"""

MavlinkCpuInfo = provider(
    doc = "The generated dialect of one CPU, input of ara_mavlink_system.",
    fields = {"dialect": "dialect name (the CPU name in errors)", "xml": "File", "links_md": "File",
              "monitor_meta": "File"},
)

_OUTS = (".xml", "_links.md", ".lua", ".py", "_monitor.json", "_monitor.py")

def _output_groups(outs):
    return OutputGroupInfo(
        dialect_py = depset([outs[3]]),
        monitor_json = depset([outs[4]]),
        monitor_py = depset([outs[5]]),
    )

def _ara_mavlink_dialect_impl(ctx):
    d = ctx.attr.dialect
    outs = [ctx.actions.declare_file(d + ext) for ext in _OUTS]
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
    return [
        DefaultInfo(files = depset(outs)),
        _output_groups(outs),
        MavlinkCpuInfo(dialect = d, xml = outs[0], links_md = outs[1], monitor_meta = outs[4]),
    ]

_ara_mavlink_dialect = rule(
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

def _ara_mavlink_system_impl(ctx):
    d = ctx.attr.dialect or ctx.label.name
    if len(ctx.attr.cpus) != len(ctx.files.daemon_configs):
        fail("ara_mavlink_system %s: cpus (%d) and daemon_configs (%d) must pair up by position" %
             (ctx.label, len(ctx.attr.cpus), len(ctx.files.daemon_configs)))
    outs = [ctx.actions.declare_file(d + ext) for ext in _OUTS]
    args = ctx.actions.args()
    args.add(outs[0].dirname)
    args.add("--dialect", d)
    inputs = list(ctx.files.daemon_configs)
    for cpu, cfg in zip(ctx.attr.cpus, ctx.files.daemon_configs):
        info = cpu[MavlinkCpuInfo]
        args.add_all("--cpu", [info.dialect, info.xml, info.links_md, cfg, info.monitor_meta])
        inputs += [info.xml, info.links_md, info.monitor_meta]
    ctx.actions.run(
        inputs = inputs,
        outputs = outs,
        arguments = [args],
        executable = ctx.executable._gen,
        mnemonic = "MavlinkSystem",
        progress_message = "Generating MAVLink system dialect %s" % d,
    )
    return [DefaultInfo(files = depset(outs)), _output_groups(outs)]

_ara_mavlink_system = rule(
    implementation = _ara_mavlink_system_impl,
    doc = """One dialect for the station from the dialects of all CPUs of a rocket.

`cpus`: `ara_mavlink_dialect` targets, one per CPU with a radio. `daemon_configs`: the
mavlink_daemon_config.json of each CPU, in the same order as `cpus`. A `msg_id` used by two CPUs
for different messages and a repeated (system_id, component_id) are generation errors that name
the CPUs. Outputs: <dialect>.xml, <dialect>_links.md, <dialect>.lua, <dialect>.py,
<dialect>_monitor.py and <dialect>_monitor.json (the monitor tells the CPUs apart by
system_id:component_id).""",
    attrs = {
        "cpus": attr.label_list(providers = [MavlinkCpuInfo], mandatory = True),
        "daemon_configs": attr.label_list(allow_files = [".json"], mandatory = True),
        "dialect": attr.string(doc = "Dialect name; defaults to the target name."),
        "_gen": attr.label(
            executable = True,
            cfg = "exec",
            default = Label("@srp_platform//tools/model_generator/ara/mavlink:mavlink_system_gen"),
        ),
    },
)

def _monitor(name, dialect, visibility, tags, testonly):
    """The filegroups of the generated monitor script and its files."""
    script = dialect + "_monitor.py"
    for group, fname in (("monitor_py", script), ("dialect_py", name + "_dialect_py"),
                         ("monitor_json", name + "_monitor_json")):
        native.filegroup(
            name = fname,
            srcs = [":" + name],
            output_group = group,
            visibility = visibility,
            tags = tags,
            testonly = testonly,
        )

def ara_mavlink_dialect(name, visibility = None, tags = None, testonly = False, **kwargs):
    """The rule `_ara_mavlink_dialect` and the filegroups of the monitor (see the file header)."""
    _ara_mavlink_dialect(name = name, visibility = visibility, tags = tags, testonly = testonly, **kwargs)
    _monitor(name, kwargs["dialect"], visibility, tags, testonly)

def ara_mavlink_system(name, visibility = None, tags = None, testonly = False, **kwargs):
    """The rule `_ara_mavlink_system` and the filegroups of the monitor (see the file header)."""
    _ara_mavlink_system(name = name, visibility = visibility, tags = tags, testonly = testonly, **kwargs)
    _monitor(name, kwargs.get("dialect") or name, visibility, tags, testonly)
