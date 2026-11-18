"""Reguła bazela: aplikacje CPU (app_config.json) -> dialekt MAVLink i artefakty mavgen.

Dialekt jest per CPU: `apps` = modele WSZYSTKICH aplikacji z `on: mavlink` jednego CPU (jeden
MavlinkDaemon); kolizja msg_id między aplikacjami = błąd generacji. `daemon_config` (opcjonalny)
= mavlink_daemon_config.json CPU: walidacja nazw łączy/wersji/podpisu i macierz łączy z ich
parametrami.

Wyjścia: <dialect>.xml, <dialect>_links.md, <dialect>.lua (WLua), <dialect>.py
(pymavlink) i katalog <dialect>_c/{v1,v2} z nagłówkami C.
"""

def _ara_mavlink_dialect_impl(ctx):
    d = ctx.attr.dialect
    xml = ctx.actions.declare_file(d + ".xml")
    md = ctx.actions.declare_file(d + "_links.md")
    lua = ctx.actions.declare_file(d + ".lua")
    py = ctx.actions.declare_file(d + ".py")
    c_dir = ctx.actions.declare_directory(d + "_c")
    versions = ",".join([str(v) for v in ctx.attr.versions])

    args = ctx.actions.args()
    args.add(xml.path)
    args.add("--links-md", md.path)
    args.add("--dialect", d)
    args.add("--allowed-versions", versions)
    inputs = ctx.files.apps + ctx.files.data
    if ctx.file.daemon_config:
        args.add("--daemon-config", ctx.file.daemon_config.path)
        inputs = inputs + [ctx.file.daemon_config]
    args.add_all(ctx.files.apps)
    ctx.actions.run(
        inputs = inputs,
        outputs = [xml, md],
        arguments = [args],
        executable = ctx.executable._gen,
        env = ctx.var,
        mnemonic = "MavlinkDialect",
        progress_message = "Generating MAVLink dialect %s" % d,
    )
    ctx.actions.run(
        inputs = [xml],
        outputs = [c_dir, lua, py],
        arguments = [xml.path, c_dir.path, lua.path, py.path, versions],
        executable = ctx.executable._mavgen,
        mnemonic = "Mavgen",
        progress_message = "Running mavgen for %s" % d,
    )
    return [DefaultInfo(files = depset([xml, md, lua, py, c_dir]))]

ara_mavlink_dialect = rule(
    implementation = _ara_mavlink_dialect_impl,
    attrs = {
        "apps": attr.label_list(allow_files = [".json"], mandatory = True),
        "daemon_config": attr.label(allow_single_file = [".json"]),
        "data": attr.label_list(allow_files = [".json"]),
        "dialect": attr.string(mandatory = True),
        # wersje MAVLink, dla których generujemy C (łącza aplikacji muszą się w nich mieścić)
        "versions": attr.int_list(default = [2]),
        "_gen": attr.label(
            executable = True,
            cfg = "exec",
            default = Label("@srp_platform//tools/model_generator/ara/mavlink:mavlink_gen"),
        ),
        "_mavgen": attr.label(
            executable = True,
            cfg = "exec",
            default = Label("@srp_platform//tools/model_generator/ara/mavlink:mavgen_run"),
        ),
    },
)
