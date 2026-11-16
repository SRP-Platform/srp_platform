def _flash_script(ctx):
    user = ctx.attr.user_name + "@" + ctx.attr.ip
    path = ctx.files.file[0].path
    path = path.split("/bin/")[-1]
    content = """ 
#!/bin/sh
################################################################################
#
#   Copyright (c) 2024 Bartosz Snieg.
#
################################################################################
#
now=`date '+%Y_%m_%d_%H_%M_%S'`
file_name=\""""+path+"""\"
echo "SRP 2.0 Flash tool [Start]"
echo "Flash tool: removing old sw"
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null """+user+""" "rm -rf /srp/opt/*"
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null """+user+""" "rm -rf /srp/platform/*"
echo "Flash tool: Uploading new sw"
scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $file_name \""""+user+""":/srp/update/new/"""+ctx.files.file[0].basename.replace(".tar","")+"""-$now.tar\"
echo "Flash tool: Unpacking a new software version"
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null """+user+""" "tar -xf /srp/update/new/"""+ctx.files.file[0].basename.replace(".tar","")+"""-$now.tar -C /srp"
echo "Flash tool: Cleaning"
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null """+user+""" " sleep 2 & rm -rf /srp/update/new/"""+ctx.files.file[0].basename.replace(".tar","")+"""-$now.tar"
""" + _post_install(ctx, user) + """echo "Flash tool: ECU reboot"
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=2  """+user+""" " sleep 2 & reboot"
echo "SRP 2.0 Flash tool [Stop]"
"""
    return content

def _post_install(ctx, user):
    # Opcjonalna komenda wykonywana na ECU po rozpakowaniu, przed restartem
    # (np. `lbu commit -d` na systemie dzialajacym z RAM).
    if ctx.attr.post_install_cmd == "":
        return ""
    return """echo "Flash tool: Post install"
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null """ + user + """ \"""" + ctx.attr.post_install_cmd + """\"
"""

def _impl(ctx):
    out = ctx.actions.declare_file("flash_script.sh")
    ctx.actions.write(output = out, content = _flash_script(ctx))
    return [DefaultInfo(files = depset([out]))]

_flash_ecu = rule(
    implementation = _impl,
    attrs = {
        "file": attr.label_list(mandatory = False, allow_files = True),
        "ip": attr.string(),
        "user_name": attr.string(),
        "post_install_cmd": attr.string(default = ""),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/common:flash_sh"),
        ),
    },
)

def _default_post_install_cmd():
    # RPi Zero 2 W dziala na Alpine w trybie diskless (system w RAM): wgrane
    # oprogramowanie trzeba utrwalic na karcie przed restartem (lbu commit).
    return select({
        "@srp_platform//bazel/platforms:is_rpi_zero2w": "lbu commit -d",
        "//conditions:default": "",
    })

def flash_ecu(name, file = [], ip = "", user_name = "", tool = "", post_install_cmd = None):
    if post_install_cmd == None:
        post_install_cmd = _default_post_install_cmd()
    native.filegroup(name = name+".data",srcs=file);
    _flash_ecu(
        name = name + ".script",
        file = file,
        ip = ip,
        user_name = user_name,
        post_install_cmd = post_install_cmd,
    )
    native.sh_binary(
        name = name,
        srcs = [":" + name + ".script",],
        data = [":"+name+".data"],
        args = file
    )
