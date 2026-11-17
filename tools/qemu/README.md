# QEMU

To run qemu you must have the following packages installed:
```
$ sudo apt-get install -y qemu-system-arm 
```

Before starting work, the network environment must be set up by running the following script:

```
$ bazel run @srp_platform//tools/qemu:network_configure 
```

`tap0` is created with the user who runs the script as its owner (`user ${SUDO_USER:-$(id -un)}`), so QEMU can
use it without root. A `tap0` without an owner makes QEMU fail with
`could not configure /dev/net/tun (tap0): Operation not permitted`.

We can then run qemu simulations via the following command:

```
$ bazel run @srp_platform//tools/qemu
```

In the above case, the console will be launched in a separate window. Requires a graphics driver.
To run without graphics drivers, use the following command. It will launch qemu in the terminal. * CTRL+X will not work.

```
bazel run @srp_platform//tools/qemu -- --console
```

## Raspberry Pi Zero 2 W (Alpine aarch64)

`:qemu_rpi` runs the software stack of the Raspberry Pi Zero 2 W board (Alpine Linux 3.22, aarch64, musl,
binaries built with `--config=rpi-platform`) on the generic QEMU `virt` machine. It is not a board emulation:
there is no GPIO, I2C, 1-Wire, WiFi or SD card - only the system, the SRP services and the network.

| | BBB (`:qemu`) | RPi (`:qemu_rpi`) |
|---|---|---|
| QEMU | `qemu-system-arm -machine virt,highmem=off -cpu cortex-a15 -smp 4 -m 256` | `qemu-system-aarch64 -machine virt -cpu cortex-a53 -smp 4 -m 512` |
| Image | `@qemu_image` (srp-poky, downloaded by bazel) | built locally into a cache (below) |
| Network | `tap0`, guest `eth0` = `192.168.10.101` | the same |
| Disk | `snapshot=on` (changes are lost on exit) | the same |
| Login | console and ssh `root` without password | console `root` without password, ssh `root` **with a key only** |

The empty root password of the RPi image is meant only for the serial console of this local guest; sshd still
accepts root with a key only (`PermitRootLogin prohibit-password`).

Extra host tools: `qemu-system-aarch64` (part of `qemu-system-arm` on Ubuntu), `curl`, `python3`, `debugfs`
(`e2fsprogs`), and an ssh key in `~/.ssh/id_ed25519.pub`. No `sudo` is needed (except for `network_configure`).

Build the image once (about 3 minutes; network access only to `dl-cdn.alpinelinux.org`):

```
bazel run @srp_platform//tools/qemu:build_rpi_image
```

Then start it like the BBB guest:

```
bazel run @srp_platform//tools/qemu:qemu_rpi -- --console
ssh root@192.168.10.101
```

In `--console` mode QEMU is closed with `CTRL+A X` (or `poweroff` in the guest).

### How the image is built

`build_rpi_image.sh` boots the pinned Alpine netboot kernel/initramfs (version and sha256 in the script) in
`qemu-system-aarch64` with user-mode networking. A boot overlay (apkovl) served from `127.0.0.1` runs a one-shot
script in the guest: `setup-disk` installs Alpine in `sys` mode on a raw virtio disk, then
`tools/rpi/provision.sh apply --qemu` adds the SRP part (packages `libstdc++ libgcc openssh`, the `/srp` tree,
`/home/root`, OpenRC services `srp` and `srp-clock`, ssh root with a key only, static `eth0`
`192.168.10.101/24`) - the same script that prepares the real board, without the hardware part. The host key
`~/.ssh/id_ed25519.pub` becomes `/root/.ssh/authorized_keys`. Finally the kernel and initramfs of the installed
system are copied out of the disk with `debugfs`.

Installing inside the guest was chosen over `fakeroot` + `mke2fs -d`: package scripts and triggers (busybox
links, `mkinitfs`, OpenRC) run natively on aarch64, so no binfmt/qemu-user or root is needed on the host, and
the result is a regular Alpine installation.

Result, reused by `:qemu_rpi` (about 190 MB on disk):

```
~/.cache/srp_platform/qemu_rpi/<alpine version>/{disk.img,vmlinuz-virt,initramfs-virt}
```

The build is idempotent: a second run does nothing. The image is rebuilt when `build_rpi_image.sh`,
`tools/rpi/provision.sh` or the ssh key change - `:qemu_rpi` refuses a missing or out-of-date image and prints the
build command. `--force` rebuilds anyway. The console log of the last build is in
`~/.cache/srp_platform/qemu_rpi/last-build.log`. Environment: `SRP_QEMU_RPI_CACHE` (cache directory),
`SRP_QEMU_RPI_PUBKEY` (public key).

### Flashing

```
bazel run --config=rpi-platform //test_cpu/cpu/ec:flash
```

`flash_ecu` runs `lbu commit -d` before the reboot on `rpi_zero2w` (the board runs Alpine diskless). In the
QEMU image the system is installed on the disk, so the files are persistent anyway (until QEMU exits, because
of `snapshot=on`); `provision.sh apply --qemu` sets `LBU_BACKUPDIR=/var/lib/srp-lbu` in `/etc/lbu/lbu.conf`,
so `lbu commit -d` only stores an overlay copy there and succeeds.

### `provision.sh check` in the guest

`sh /root/provision.sh check` must report no `[FAIL]` except the hardware items below. `[WARN]` about the
diskless mode, the apk cache, `/data` and `/logs` partitions, 1-Wire and the software clock are expected.

| Allowed `[FAIL]` | Why |
|---|---|
| `brak /sys/class/gpio` | the Alpine `virt` kernel has no `CONFIG_GPIO_SYSFS` and the `virt` machine has no board GPIO |