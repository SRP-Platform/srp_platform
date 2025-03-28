load("@bazel_tools//tools/build_defs/pkg:pkg.bzl", "pkg_tar")
load("@srp_platform//tools/common:connect_tar.bzl", "connect_tar")

def _netinterface_script(ctx):
    json_data = json.decode(ctx.apis.read(ctx.files.configs[0].path))
    content = """ 
#!/bin/sh
################################################################################
#
#   Copyright (c) 2024 Bartosz Snieg.
#
################################################################################
#
echo "Setting interface: """ + json_data.encode("interface_name") + """ for """ + json_data.encode("name") + """ "
echo "ip: """ + json_data.encode("ip") + """"
echo "net mask """ + json_data.encode("mask") + """ "
ifconfig """ + json_data.encode("interface_name") + """ """ + json_data.encode("ip") + """ netmask """ + json_data.encode("mask") + """
echo "Interface set [DONE]"
    """
    return content

def convert(path):
    res = path.split("/")[-1].replace(".tar", "")
    return res

def _start_service_script(ctx):
    content = """ 
#!/bin/sh
################################################################################
#
#   Copyright (c) 2024 Bartosz Snieg.
#
################################################################################
#
echo "Starting components SRP EM "
sleep 1
setsid /srp/platform/em/bin/em &
setsid /srp/platform/ipc_service_discover/bin/ipc_service_discover &
setsid /srp/platform/state_manager/bin/state_manager &
echo "Simab SRP start up component script [DONE]"
"""
    return content

def _startup_script(ctx):
    content = """ 
#!/bin/sh
################################################################################
#
#   Copyright (c) 2024 Bartosz Snieg.
#
################################################################################
#
echo "Simab SRP start up script"

/srp/opt/cpu_srp/network_interface.sh
/srp/opt/cpu_srp/component_start_up.sh &

echo "Simab SRP start up script [DONE]"

"""
    return content

def _start_service_list_impl(ctx):
    out1 = ctx.actions.declare_file("start_up.sh")
    out2 = ctx.actions.declare_file("component_start_up.sh")
    out3 = ctx.actions.declare_file("network_interface.sh")
    ctx.actions.write(output = out1, content = _startup_script(ctx))
    ctx.actions.write(output = out2, content = _start_service_script(ctx))

    # ctx.actions.write(output = out3, content = _netinterface_script(ctx))
    # out = ctx.actions.declare_file("srp_app.json")
    args = [out3.path, ctx.files.configs[0].path]

    # Action to call the script.
    ctx.actions.run(
        inputs = ctx.files.configs,
        outputs = [out3],
        arguments = args,
        executable = ctx.executable.tool,
    )
    return [DefaultInfo(files = depset([out1, out2, out3]))]

cpu_def_r = rule(
    implementation = _start_service_list_impl,
    attrs = {
        "interface_name": attr.string(),
        "configs": attr.label_list(mandatory = False, allow_files = True),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/cpu:cpu_gen"),
        ),
    },
)

def cpu_def(name, srp_components, config, etcs = [], platform_etcs = []):
    cpu_def_r(
        name = "cpu",
        configs = [config],
    )
    pkg_tar(
        name = "config_pkg",
        package_dir = "opt/cpu_srp",
        srcs = [":cpu"] + etcs,
        visibility = ["//visibility:private"],
    )
    pkg_tar(
        name = "platform_config_pkg",
        package_dir = "platform/etc",
        srcs = platform_etcs,
        visibility = ["//visibility:private"],
    )
    connect_tar(
        name = name,
        srcs = [":config_pkg", ":platform_config_pkg"] + srp_components,
        visibility = ["//visibility:public"],
    )
