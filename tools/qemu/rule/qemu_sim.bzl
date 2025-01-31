def _get_script(ctx):
    return """
unset GTK_PATH

if [ "$1" == "--console" ]; then
    qemu-system-arm -device virtio-net-device,netdev=net0,mac=52:54:00:12:34:02 -netdev tap,id=net0,ifname=tap0,script=no,downscript=no -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
    -drive id=disk0,file=$PWD/"""+ctx.files.file_ext4[0].path+""",if=none,format=raw,snapshot=on -device virtio-blk-device,drive=disk0 -device qemu-xhci -device usb-tablet -device usb-kbd  -machine virt,highmem=off -cpu cortex-a15 -smp 4 -m 256 -nographic \
    -kernel $PWD/"""+ctx.files.file_bin[0].path+""" -append 'root=/dev/vda rw  mem=256M ip=192.168.7.2::192.168.7.1:255.255.255.0::eth0:off:8.8.8.8 net.ifnames=0 swiotlb=0 '
else
    qemu-system-arm -device virtio-net-device,netdev=net0,mac=52:54:00:12:34:02 -netdev tap,id=net0,ifname=tap0,script=no,downscript=no -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
    -drive id=disk0,file=$PWD/"""+ctx.files.file_ext4[0].path+""",if=none,format=raw,snapshot=on -device virtio-blk-device,drive=disk0 -device qemu-xhci -device usb-tablet -device usb-kbd  -machine virt,highmem=off -cpu cortex-a15 -smp 4 -m 256 -serial mon:vc -serial null \
    -kernel $PWD/"""+ctx.files.file_bin[0].path+""" -append 'root=/dev/vda rw  mem=256M ip=192.168.7.2::192.168.7.1:255.255.255.0::eth0:off:8.8.8.8 net.ifnames=0 swiotlb=0 '
fi
"""

def _impl(ctx):
    out = ctx.actions.declare_file("run_script.sh")
    ctx.actions.write(output = out, content = _get_script(ctx))
    return [DefaultInfo(files = depset([out]))]

_run_qemu = rule(
    implementation = _impl,
    attrs = {
        "file_ext4": attr.label_list(mandatory = False, allow_files = True),
        "file_bin": attr.label_list(mandatory = False, allow_files = True),
    },
)

def run_qemu(name,file_ext4,file_bin):

    _run_qemu(
        name=name+".qemu",
        file_ext4 = file_ext4,
        file_bin = file_bin,
    )

    native.sh_binary(
        name = name,
        srcs = [":"+name+".qemu"],
        data = []+file_bin+file_ext4,
    )