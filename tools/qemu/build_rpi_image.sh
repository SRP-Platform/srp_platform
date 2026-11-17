#!/bin/bash
################################################################################
#
#   Builds the Alpine Linux aarch64 disk image used by
#   @srp_platform//tools/qemu:qemu_rpi (Raspberry Pi Zero 2 W software stack on
#   the QEMU "virt" machine). No root/sudo needed.
#
#   How: the pinned Alpine netboot kernel/initramfs/modloop boot in
#   qemu-system-aarch64 (user-mode network, guest only talks to the Alpine
#   mirror). A boot overlay (apkovl) runs a one-shot script in the guest that
#   installs Alpine in "sys" mode on a raw virtio disk (setup-disk), runs
#   tools/rpi/provision.sh apply --qemu in it and powers off. The kernel and
#   initramfs of the installed system are then copied out of the disk image
#   with debugfs.
#
#   Usage: build_rpi_image [--force] [--print-dir]
#     --force       rebuild even if the cached image is up to date
#     --print-dir   only print the image directory if it is up to date
#                   (exit 1 otherwise); used by :qemu_rpi
#
#   Output (cache, outside bazel-out and /tmp):
#     ${SRP_QEMU_RPI_CACHE:-~/.cache/srp_platform/qemu_rpi}/<alpine version>/
#       disk.img  vmlinuz-virt  initramfs-virt  .complete
#   The host public key (${SRP_QEMU_RPI_PUBKEY:-~/.ssh/id_ed25519.pub}) goes to
#   /root/.ssh/authorized_keys of the guest.
#
################################################################################
set -euo pipefail

ALPINE_VERSION="3.24.4"
ALPINE_BRANCH="v3.24"
MIRROR="https://dl-cdn.alpinelinux.org/alpine"
NETBOOT_URL="$MIRROR/$ALPINE_BRANCH/releases/aarch64/netboot-$ALPINE_VERSION"
# sha256 of the netboot files (same as in alpine-netboot-3.24.4-aarch64.tar.gz,
# sha256 9d10f909d41b8393a80a645e9e2af761fcb1f29b986799edeedfa269c359745e).
NETBOOT_FILES="
vmlinuz-virt cd05fed9e617b5df93dc62ece9b78c422fd94e9b432a2f7c092714b139d5eef4
initramfs-virt 189402937646965374ef2df5d37f76467135179ae168c88175df2fe3e264a0b0
modloop-virt a726889467d1d4fefa4ba1ed710f28b9db1fbd019ca3eba2d4c6c65b33536343
"
DISK_SIZE="2G"
BUILD_TIMEOUT=1800

CACHE_ROOT="${SRP_QEMU_RPI_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/srp_platform/qemu_rpi}"
OUT="$CACHE_ROOT/$ALPINE_VERSION"
PUBKEY="${SRP_QEMU_RPI_PUBKEY:-$HOME/.ssh/id_ed25519.pub}"

FORCE=0
PRINT_DIR=0
for arg in "$@"; do
  case "$arg" in
    --force) FORCE=1 ;;
    --print-dir) PRINT_DIR=1 ;;
    *) echo "Usage: build_rpi_image [--force] [--print-dir]" >&2; exit 2 ;;
  esac
done

log() { echo "[build_rpi_image] $*" >&2; }
die() { log "ERROR: $*"; exit 1; }

find_provision() {
  local c
  for c in \
    "${SRP_RPI_PROVISION:-}" \
    "$(dirname "$0")/../rpi/provision.sh" \
    "${RUNFILES_DIR:-$0.runfiles}/srp_platform/tools/rpi/provision.sh" \
    "$PWD/../srp_platform/tools/rpi/provision.sh" \
    "$PWD/external/srp_platform/tools/rpi/provision.sh"; do
    if [ -n "$c" ] && [ -f "$c" ]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

PROVISION="$(find_provision)" || die "tools/rpi/provision.sh not found (set SRP_RPI_PROVISION)"
[ -f "$PUBKEY" ] || die "no public key $PUBKEY (ssh-keygen -t ed25519 or set SRP_QEMU_RPI_PUBKEY)"

# Everything that changes the image content: this script, provision.sh, the key.
input_hash() {
  cat "$0" "$PROVISION" "$PUBKEY" | sha256sum | cut -d' ' -f1
}
HASH="$(input_hash)"

is_up_to_date() {
  [ -f "$OUT/.complete" ] && [ "$(cat "$OUT/.complete")" = "$HASH" ] &&
    [ -s "$OUT/disk.img" ] && [ -s "$OUT/vmlinuz-virt" ] && [ -s "$OUT/initramfs-virt" ]
}

if [ "$PRINT_DIR" -eq 1 ]; then
  if is_up_to_date; then
    echo "$OUT"
    exit 0
  fi
  log "QEMU RPi image missing or out of date in $OUT."
  log "Build it with: bazel run @srp_platform//tools/qemu:build_rpi_image"
  exit 1
fi

mkdir -p "$CACHE_ROOT"
exec 9> "$CACHE_ROOT/.lock"
flock -n 9 || { log "waiting for another build_rpi_image ($CACHE_ROOT/.lock)"; flock 9; }

if [ "$FORCE" -eq 0 ] && is_up_to_date; then
  log "image up to date: $OUT"
  exit 0
fi

for tool in qemu-system-aarch64 curl python3 debugfs sha256sum; do
  command -v "$tool" > /dev/null || die "missing host tool: $tool"
done

WORK="$(mktemp -d "$CACHE_ROOT/.work.XXXXXX")"
HTTP_PID=""
cleanup() {
  [ -n "$HTTP_PID" ] && kill "$HTTP_PID" 2> /dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

START=$(date +%s)

# 1. Netboot kernel, initramfs and modloop (pinned).
mkdir -p "$WORK/www"
echo "$NETBOOT_FILES" | while read -r name sha; do
  [ -n "$name" ] || continue
  log "downloading $name"
  curl -fsSL --retry 3 -o "$WORK/www/$name" "$NETBOOT_URL/$name" || die "download of $name failed"
  echo "$sha  $WORK/www/$name" | sha256sum -c --quiet - || die "sha256 mismatch: $name"
done

# 2. Boot overlay (apkovl) with the one-shot installer.
OVL="$WORK/ovl"
mkdir -p "$OVL/etc/apk" "$OVL/etc/local.d" "$OVL/etc/runlevels/default" "$OVL/srp-build"
echo "srp-build" > "$OVL/etc/hostname"
printf 'alpine-base\ne2fsprogs\n' > "$OVL/etc/apk/world"
printf '%s/%s/main\n%s/%s/community\n' "$MIRROR" "$ALPINE_BRANCH" "$MIRROR" "$ALPINE_BRANCH" \
  > "$OVL/etc/apk/repositories"
touch "$OVL/etc/.default_boot_services"
ln -s /etc/init.d/local "$OVL/etc/runlevels/default/local"
cp "$PROVISION" "$OVL/srp-build/provision.sh"
cp "$PUBKEY" "$OVL/srp-build/authorized_keys"
cat > "$OVL/etc/local.d/srp-build.start" << 'EOF'
#!/bin/sh
# One-shot: install Alpine (sys mode) on /dev/vda for :qemu_rpi, then power off.
exec > /dev/console 2>&1
S=/srp-build
fail() {
  echo "SRP-IMAGE-BUILD: FAIL $*"
  sync
  poweroff -f
  exit 1
}
echo "SRP-IMAGE-BUILD: start"
udhcpc -i eth0 -n -q -t 10 > /dev/null 2>&1 || fail "no network (dhcp)"
apk add -q e2fsprogs || fail "apk add e2fsprogs"
mkfs.ext4 -q -F -L srp-root /dev/vda || fail "mkfs.ext4"
mount -t ext4 /dev/vda /mnt || fail "mount /dev/vda"
BOOTLOADER=none setup-disk -m sys -k virt /mnt || fail "setup-disk"
rm -f /mnt/etc/local.d/srp-build.start /mnt/etc/runlevels/default/local

install -d -m 700 /mnt/root/.ssh
install -m 600 "$S/authorized_keys" /mnt/root/.ssh/authorized_keys
install -m 755 "$S/provision.sh" /mnt/root/provision.sh
cp /etc/resolv.conf /mnt/etc/resolv.conf
mount -t proc proc /mnt/proc
mount -t sysfs sys /mnt/sys
mount -o bind /dev /mnt/dev
chroot /mnt sh /root/provision.sh apply --qemu > /tmp/apply.log 2>&1
cat /tmp/apply.log
grep -q "lbu commit wykonany" /tmp/apply.log || fail "provision.sh apply --qemu (lbu commit)"
grep -q "libstdc++ i libgcc sa" /tmp/apply.log || fail "provision.sh apply --qemu (libstdc++)"
# Host keys generated once here (TCG is slow, snapshot=on would redo it every boot).
chroot /mnt ssh-keygen -A || fail "ssh-keygen -A"

# Serial console (ttyAMA0): root without password, only on the console -
# sshd allows root with a key only (provision.sh: prohibit-password).
sed -i 's/^root:[^:]*:/root::/' /mnt/etc/shadow
grep -q '^ttyAMA0::' /mnt/etc/inittab ||
  echo 'ttyAMA0::respawn:/sbin/getty -L 0 ttyAMA0 vt100' >> /mnt/etc/inittab
grep -qx 'ttyAMA0' /mnt/etc/securetty || echo 'ttyAMA0' >> /mnt/etc/securetty
rm -f /mnt/etc/resolv.conf

umount /mnt/dev /mnt/sys /mnt/proc
umount /mnt || fail "umount /mnt"
sync
echo "SRP-IMAGE-BUILD: OK"
poweroff -f
EOF
chmod 755 "$OVL/etc/local.d/srp-build.start"
tar --owner=0 --group=0 -czf "$WORK/www/srp-build.apkovl.tar.gz" -C "$OVL" .

# 3. Local HTTP server (127.0.0.1 only) for the overlay and modloop;
#    QEMU user network maps it to 10.0.2.2 in the guest.
PORT="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"
python3 -m http.server --bind 127.0.0.1 --directory "$WORK/www" "$PORT" > "$WORK/http.log" 2>&1 &
HTTP_PID=$!
HOST_URL="http://10.0.2.2:$PORT"

# 4. Install in the guest.
truncate -s "$DISK_SIZE" "$WORK/disk.img"
CONSOLE_LOG="$CACHE_ROOT/last-build.log"
log "installing Alpine $ALPINE_VERSION in qemu-system-aarch64 (console log: $CONSOLE_LOG)"
rc=0
timeout "$BUILD_TIMEOUT" qemu-system-aarch64 \
  -machine virt -cpu cortex-a53 -smp 4 -m 1024 \
  -display none -monitor none -serial "file:$CONSOLE_LOG" -no-reboot \
  -kernel "$WORK/www/vmlinuz-virt" -initrd "$WORK/www/initramfs-virt" \
  -append "console=ttyAMA0 ip=dhcp modules=loop,squashfs alpine_repo=$MIRROR/$ALPINE_BRANCH/main modloop=$HOST_URL/modloop-virt apkovl=$HOST_URL/srp-build.apkovl.tar.gz" \
  -netdev user,id=net0 -device virtio-net-device,netdev=net0 \
  -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-device,rng=rng0 \
  -drive id=disk0,file="$WORK/disk.img",if=none,format=raw -device virtio-blk-device,drive=disk0 \
  || rc=$?
grep -q "SRP-IMAGE-BUILD: OK" "$CONSOLE_LOG" ||
  die "guest install failed (qemu rc=$rc): $(grep -a 'SRP-IMAGE-BUILD' "$CONSOLE_LOG" | tail -1); see $CONSOLE_LOG"

# 5. Kernel and initramfs of the installed system (match /lib/modules on disk).
for f in vmlinuz-virt initramfs-virt; do
  debugfs -R "dump /boot/$f $WORK/$f" "$WORK/disk.img" > /dev/null 2>&1
  [ -s "$WORK/$f" ] || die "/boot/$f not found in the image"
done

rm -rf "$OUT"
mkdir -p "$OUT"
mv "$WORK/disk.img" "$WORK/vmlinuz-virt" "$WORK/initramfs-virt" "$OUT/"
echo "$HASH" > "$OUT/.complete"
log "image ready in $OUT ($(du -sh "$OUT" | cut -f1), $(($(date +%s) - START)) s)"
