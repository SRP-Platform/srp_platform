_BBB_APPEND = "root=/dev/vda rw  mem=256M ip=192.168.7.2::192.168.7.1:255.255.255.0::eth0:off:8.8.8.8 net.ifnames=0 swiotlb=0 "

def _qemu_cmd(ctx, disk, kernel, initrd, display):
    initrd_opt = ""
    if initrd:
        initrd_opt = " -initrd " + initrd

    # "\" + nowa linia w """...""" to kontynuacja Starlarka - w skrypcie jedna linia.
    return """    """ + ctx.attr.binary + """ -device virtio-net-device,netdev=net0,mac=52:54:00:12:34:02 -netdev tap,id=net0,ifname=tap0,script=no,downscript=no -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
    -drive id=disk0,file=""" + disk + """,if=none,format=raw,snapshot=on -device virtio-blk-device,drive=disk0 -device qemu-xhci -device usb-tablet -device usb-kbd  -machine """ + ctx.attr.machine + """ -cpu """ + ctx.attr.cpu + """ -smp """ + ctx.attr.smp + """ -m """ + ctx.attr.mem + """ """ + display + """ \
    -kernel """ + kernel + initrd_opt + """ -append '""" + ctx.attr.append + """'
"""

def _get_script(ctx):
    # Obraz z bazela (BBB: @qemu_image) albo sciezki w skrypcie (np. obraz RPi
    # z cache - katalog ustala `image_builder --print-dir` w $IMG_DIR).
    disk = ctx.attr.disk
    if not disk:
        disk = "$PWD/" + ctx.files.file_ext4[0].path
    kernel = ctx.attr.kernel
    if not kernel:
        kernel = "$PWD/" + ctx.files.file_bin[0].path
    prelude = ""
    if ctx.attr.image_builder:
        prelude = """IMG_DIR="$("$PWD/""" + ctx.executable.image_builder.short_path + """" --print-dir)" || exit 1
"""
    return """
unset GTK_PATH
""" + prelude + """
if [ "$1" == "--console" ]; then
""" + _qemu_cmd(ctx, disk, kernel, ctx.attr.initrd, "-nographic") + """else
""" + _qemu_cmd(ctx, disk, kernel, ctx.attr.initrd, "-serial mon:vc -serial null") + """fi
"""

def _impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.script_name)
    ctx.actions.write(output = out, content = _get_script(ctx))
    return [DefaultInfo(files = depset([out]))]

_run_qemu = rule(
    implementation = _impl,
    attrs = {
        "file_ext4": attr.label_list(mandatory = False, allow_files = True),
        "file_bin": attr.label_list(mandatory = False, allow_files = True),
        "binary": attr.string(default = "qemu-system-arm"),
        "machine": attr.string(default = "virt,highmem=off"),
        "cpu": attr.string(default = "cortex-a15"),
        "smp": attr.string(default = "4"),
        "mem": attr.string(default = "256"),
        "disk": attr.string(default = ""),
        "kernel": attr.string(default = ""),
        "initrd": attr.string(default = ""),
        "append": attr.string(default = _BBB_APPEND),
        "image_builder": attr.label(executable = True, cfg = "target", allow_files = True),
        "script_name": attr.string(default = "run_script.sh"),
    },
)

def run_qemu(
        name,
        file_ext4 = [],
        file_bin = [],
        binary = "qemu-system-arm",
        machine = "virt,highmem=off",
        cpu = "cortex-a15",
        smp = "4",
        mem = "256",
        disk = "",
        kernel = "",
        initrd = "",
        append = _BBB_APPEND,
        image_builder = None,
        script_name = "run_script.sh"):
    """Skrypt uruchamiajacy QEMU na tap0 (dysk snapshot=on, --console = terminal).

    Domyslne parametry = gosc BBB (qemu-system-arm, obraz @qemu_image).
    disk/kernel/initrd: sciezki w skrypcie zamiast plikow z bazela; z
    image_builder skrypt najpierw ustala $IMG_DIR (`<builder> --print-dir`).
    script_name: nazwa generowanego skryptu - rozna dla kazdego celu w pakiecie.
    """
    _run_qemu(
        name = name + ".qemu",
        file_ext4 = file_ext4,
        file_bin = file_bin,
        binary = binary,
        machine = machine,
        cpu = cpu,
        smp = smp,
        mem = mem,
        disk = disk,
        kernel = kernel,
        initrd = initrd,
        append = append,
        image_builder = image_builder,
        script_name = script_name,
    )

    native.sh_binary(
        name = name,
        srcs = [":" + name + ".qemu"],
        data = [] + file_bin + file_ext4 + ([image_builder] if image_builder else []),
    )
