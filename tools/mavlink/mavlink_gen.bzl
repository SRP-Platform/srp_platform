def _mavlink_gen_impl(ctx):
    # The list of arguments we pass to the script.
    out = ctx.actions.declare_directory("mavlink_lib.h")
    args = [
        out.path,
        ctx.files.src[0].path,
        ctx.attr.language,
        ctx.attr.wire_protocol,
    ]

    # Action to call the script.
    ctx.actions.run(
        inputs = ctx.files.src,
        outputs = [out],
        arguments = args,
        executable = ctx.executable.tool,
        env = ctx.var,
    )

    return [DefaultInfo(files = depset([out]))]

_mavlink_gen = rule(
    implementation = _mavlink_gen_impl,
    attrs = {
        "src": attr.label(mandatory = False, allow_files = True),
        "language": attr.string(),
        "wire_protocol": attr.string(),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/mavlink:gen_mavlink_defs"),
        ),
    },
)


def mavlink_gen(name,src, language = "C", wire_protocol = "1.0", visibility = ["//visibility:private"]):
    _mavlink_gen(
        name = name + ".files",
        src = src,
        language = language,
        wire_protocol = wire_protocol,
        visibility = ["//visibility:private"],
    )

    native.cc_library(
        name = name,
        visibility = visibility,
        srcs = [":" + name + ".files"],
        includes = ["./mavlink_lib.h"],
    )
