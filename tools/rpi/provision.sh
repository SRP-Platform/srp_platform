#!/bin/sh
################################################################################
#  SRP - przygotowanie Raspberry Pi Zero 2 W jako komputera SRP (zamiennik BBB)
#
#  System bazowy: Alpine Linux aarch64 (obraz rpi), tryb diskless (system w RAM).
#  Binarki SRP: bazel --config=rpi-platform (aarch64, musl, linkowane dynamicznie)
#  - korzystaja z bibliotek systemu: musl oraz pakiety libstdc++ i libgcc
#  (musza byc co najmniej tak nowe jak w toolchainie: gcc 14.3 / GLIBCXX_3.4.33).
#
#  Uruchom NA RPi jako root:   sh provision.sh <tryb> [opcje]
#
#    check              tylko sprawdza system, niczego nie zmienia (domyslne)
#    partitions [data] [logs]
#                       tworzy trwale partycje ext4: /data (dane z lotu, domyslnie
#                       16G) i /logs (logi, domyslnie 8G); dopisuje je w wolnym
#                       miejscu karty, niczego nie kasuje. Razem z partycja
#                       systemowa miesci sie na karcie 32 GB.
#    apply              konfiguruje system (idempotentne) i robi `lbu commit`
#
#  Opcje (apply):
#    --w1-pin <bcm>     pin 1-Wire (domyslnie 4)
#    --keep-bt          zostaw Bluetooth (UART na GPIO14/15 bedzie mini-UART ttyS0)
#    --no-commit        nie wykonuj `lbu commit`
#    --hostname <nazwa> nazwa hosta (domyslnie srp-rpi)
#    --no-usb-console   bez konsoli serwisowej na porcie USB (ttyGS0)
#
#  Uklad karty:
#    p1  system (FAT, tylko odczyt): Alpine + apkovl; wczytywany do RAM przy starcie.
#        /etc i /srp (oprogramowanie) zyja w RAM i sa utrwalane przez `lbu commit`
#        - robi to flash_ec_rpi po wgraniu nowego oprogramowania.
#    p2  /data  dane z lotu (ext4, zapis na karte)
#    p3  /logs  logi (ext4, zapis na karte)
#
#  Siec: wspolna konfiguracja SRP uzywa interfejsu "eth0" (takze DoIP). Zero 2 W
#  nie ma Ethernetu, wiec usluga srp-net nadaje te nazwe karcie WiFi - adres SRP
#  i multicast dzialaja wtedy wprost po WiFi (stacja naziemna), a po zerwaniu
#  WiFi interfejs i adres zostaja, wiec komunikacja lokalna trwa dalej. Gdy
#  WiFi nie ma wcale, eth0 jest atrapa (dummy). Prawdziwy eth0 (USB-Ethernet)
#  ma pierwszenstwo. Dodatkowy adres z DHCP (etykieta eth0:svc) sluzy serwisowi.
#
#  Zegar: plytka nie ma RTC - usluga srp-clock co minute zapisuje czas na /logs
#  i odtwarza go przy starcie (czas nie cofa sie po restarcie / zaniku zasilania).
#
#  Co odtwarza z obrazu Yocto BBB (srp-poky, /etc/init.d/srp_start.sh):
#    /srp/{opt,platform,update/{new,backup,current},token}, /home/root,
#    start /srp/opt/cpu_srp/start_up.sh przy boocie, ssh root@ (flash_ecu),
#    1-Wire, I2C, UART.
#  Nazw urzadzen (/dev/i2c-*, /dev/tty*) i numerow GPIO NIE mapuje - to robi
#  platforma w kodzie (select na @srp_platform//bazel/platforms:is_rpi_zero2w).
################################################################################
set -u

MODE="check"
W1_PIN=4
KEEP_BT=0
COMMIT=1
HOSTNAME_NEW="srp-rpi"
USB_CONSOLE=1
DATA_SIZE="16G"
LOGS_SIZE="8G"
DISK=/dev/mmcblk0
SIZES=""

[ $# -gt 0 ] && { MODE="$1"; shift; }
while [ $# -gt 0 ]; do
  case "$1" in
    --w1-pin) W1_PIN="$2"; shift ;;
    --keep-bt) KEEP_BT=1 ;;
    --no-commit) COMMIT=0 ;;
    --hostname) HOSTNAME_NEW="$2"; shift ;;
    --no-usb-console) USB_CONSOLE=0 ;;
    [0-9]*) SIZES="$SIZES $1" ;;
    *) echo "Nieznana opcja: $1"; exit 2 ;;
  esac
  shift
done

# shellcheck disable=SC2086
set -- $SIZES
[ $# -ge 1 ] && DATA_SIZE="$1"
[ $# -ge 2 ] && LOGS_SIZE="$2"

FAIL=0
ok()   { echo "[ OK ] $*"; }
warn() { echo "[WARN] $*"; }
bad()  { echo "[FAIL] $*"; FAIL=1; }
info() { echo "[INFO] $*"; }
die()  { echo "[FAIL] $*"; exit 1; }

# Partycja startowa Alpine (FAT, montowana tylko do odczytu).
BOOT="$(awk '$2 ~ "^/media/mmcblk0p1$" {print $2}' /proc/mounts | head -1)"
[ -n "$BOOT" ] || BOOT=/media/mmcblk0p1
USERCFG="$BOOT/usercfg.txt"

################################################################################
check_system() {
  echo "=== Kontrola systemu ==="
  info "Model: $(tr -d '\0' < /proc/device-tree/model 2>/dev/null); kernel $(uname -r) $(uname -m)"
  [ -f /etc/alpine-release ] && ok "Alpine $(cat /etc/alpine-release)" || bad "to nie Alpine Linux"
  [ "$(uname -m)" = "aarch64" ] && ok "aarch64 (zgodne z --config=rpi-platform)" || bad "architektura $(uname -m), oczekiwano aarch64"

  if awk '$2=="/" {print $3}' /proc/mounts | grep -q tmpfs; then ok "tryb diskless (/ w RAM)"
  else warn "/ nie jest w RAM - system zainstalowany w trybie sys?"; fi

  if [ -L /etc/apk/cache ]; then ok "cache apk: $(readlink /etc/apk/cache)"
  else warn "brak cache apk (setup-apkcache) - w trybie diskless pakiety nie wroca po restarcie bez sieci"; fi

  for mp in /data /logs; do
    if mountpoint -q "$mp" 2>/dev/null; then ok "$mp na karcie ($(awk -v m="$mp" '$2==m{print $1" "$3}' /proc/mounts), wolne $(df -h "$mp" | awk 'NR==2{print $4}'))"
    else warn "$mp nie jest zamontowane (uruchom: partitions)"; fi
  done
  [ -d /srp/update/new ] && ok "/srp/update/new jest" || warn "brak drzewa /srp"
  lbu include -l 2>/dev/null | grep -qx "srp" && ok "/srp objete przez lbu (utrwalane przy commit)" || warn "/srp nie jest objete przez lbu - oprogramowanie zniknie po restarcie"
  [ -d /home/root ] && ok "/home/root jest ($(readlink -f /home/root))" || warn "brak /home/root (FileLoggerApp)"

  if [ -d /sys/class/gpio ]; then
    ok "sysfs GPIO dostepne, baza gpiochip = $(cat /sys/class/gpio/gpiochip*/base 2>/dev/null | sort -n | head -1)"
  else
    bad "brak /sys/class/gpio - core/gpio (sysfs) nie zadziala na tym kernelu"
  fi
  # shellcheck disable=SC2012
  info "I2C: $(ls /dev/i2c-* 2>/dev/null | tr '\n' ' ')"
  # shellcheck disable=SC2012
  info "UART: $(ls /dev/ttyAMA* /dev/ttyS* 2>/dev/null | tr '\n' ' ')"
  [ -d /sys/bus/w1/devices ] && ok "1-Wire aktywne: $(ls /sys/bus/w1/devices | tr '\n' ' ')" || warn "1-Wire nieaktywne (apply + reboot)"

  if grep -aq "GLIBCXX_3.4.33" /usr/lib/libstdc++.so.6 2>/dev/null && [ -e /usr/lib/libgcc_s.so.1 ]; then
    ok "libstdc++ i libgcc sa (wymagane przez binarki SRP)"
  else
    bad "brak libstdc++ (>= GLIBCXX_3.4.33) lub libgcc - binarki SRP nie uruchomia sie"
  fi
  command -v ifconfig >/dev/null && ok "ifconfig jest (network_interface.sh)" || bad "brak ifconfig"
  command -v scp >/dev/null && ok "scp jest (flash_ecu)" || warn "brak scp (apk add openssh)"
  [ -s /root/.ssh/authorized_keys ] && ok "root ma authorized_keys" || warn "root bez authorized_keys - flash_ecu nie zaloguje sie"
  rc-update show default 2>/dev/null | grep -q '^ *srp ' && ok "usluga srp wlaczona" || warn "usluga srp nie jest wlaczona"
  if [ -e /sys/class/net/eth0 ]; then
    kind="przewodowy"; [ -d /sys/class/net/eth0/wireless ] && kind="WiFi"
    [ -d /sys/devices/virtual/net/eth0 ] && kind="atrapa (dummy)"
    ok "eth0 jest: $kind, $(ip -o -4 addr show dev eth0 | awk '{printf "%s ", $4}')"
  else
    warn "brak interfejsu eth0 (usluga srp-net)"
  fi
  [ -f /logs/.srp_clock ] && ok "zegar programowy: ostatni zapis $(date -d "@$(cat /logs/.srp_clock)" '+%Y-%m-%d %H:%M:%S' 2>/dev/null)" || warn "zegar programowy nieaktywny (srp-clock)"

  info "Interfejsy: $(ls /sys/class/net | tr '\n' ' ')"
  info "RAM: $(awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{printf "%d MB, wolne %d MB", t/1024, a/1024}' /proc/meminfo)"
}

################################################################################
make_part() {  # make_part <nr> <rozmiar> <etykieta> <punkt montowania>
  part="${DISK}p$1"; size="$2"; label="$3"; mp="$4"

  if [ ! -b "$part" ]; then
    info "dopisuje partycje $part ($size) w wolnym miejscu (istniejace bez zmian)"
    echo ",${size},L" | sfdisk --append --no-reread --force "$DISK" || die "sfdisk nie powiodl sie"
    partx -a "$DISK" 2>/dev/null; mdev -s 2>/dev/null
    if [ ! -b "$part" ]; then
      echo "Partycja zapisana, ale kernel jej jeszcze nie widzi. Zrestartuj i uruchom 'partitions' ponownie."
      exit 0
    fi
  fi

  fstype="$(blkid -o value -s TYPE "$part" 2>/dev/null)"
  cur="$(blkid -o value -s LABEL "$part" 2>/dev/null)"
  if [ -z "$fstype" ]; then
    mkfs.ext4 -q -L "$label" "$part" || die "mkfs.ext4 $part nie powiodl sie"
    info "$part: utworzono ext4 (etykieta $label)"
  elif [ "$fstype" != "ext4" ] || [ "$cur" != "$label" ]; then
    die "$part zawiera juz '$fstype' (etykieta '$cur') - nie ruszam. Sprawdz karte recznie."
  else
    info "$part jest juz przygotowana"
  fi

  mkdir -p "$mp"
  grep -q "LABEL=$label " /etc/fstab || echo "LABEL=$label $mp ext4 rw,noatime 0 2" >> /etc/fstab
  mountpoint -q "$mp" || mount "$mp" || die "nie udalo sie zamontowac $mp"
  ok "$mp zamontowane z $part"
}

do_partitions() {
  echo "=== Partycje na karcie: /data ($DATA_SIZE), /logs ($LOGS_SIZE) ==="
  [ -b "$DISK" ] || die "brak $DISK"
  apk add -q e2fsprogs sfdisk partx || die "apk add e2fsprogs sfdisk partx nie powiodlo sie"
  [ -f /root/partition-table.before.txt ] || sfdisk -d "$DISK" > /root/partition-table.before.txt
  make_part 2 "$DATA_SIZE" srp-data /data
  make_part 3 "$LOGS_SIZE" srp-logs /logs
  rc-update add localmount boot 2>/dev/null
  [ "$COMMIT" -eq 1 ] && lbu commit -d
}

################################################################################
set_usercfg_line() {  # dopisuje linie do usercfg.txt, jesli jej tam nie ma
  grep -qxF "$1" "$USERCFG" 2>/dev/null && return 0
  echo "$1" >> "$USERCFG"
  info "usercfg.txt += $1"
}

apply_alpine() {
  echo "=== Alpine: trwala konfiguracja (lbu, cache, siec) ==="
  echo "$HOSTNAME_NEW" > /etc/hostname
  hostname -F /etc/hostname
  grep -q "$HOSTNAME_NEW" /etc/hosts 2>/dev/null || echo "127.0.0.1 $HOSTNAME_NEW localhost" > /etc/hosts

  # Stan systemu (/etc, /srp) i cache pakietow - na partycji systemowej karty.
  setup-lbu mmcblk0p1 || warn "setup-lbu nie powiodl sie"
  mount -o remount,rw "$BOOT" && mkdir -p "$BOOT/cache"
  setup-apkcache "$BOOT/cache" >/dev/null 2>&1 || warn "setup-apkcache nie powiodl sie"

  # Repozytoria sieciowe (obok lokalnego na karcie) - do doinstalowania narzedzi.
  ver="$(cut -d. -f1,2 /etc/alpine-release)"
  for r in main community; do
    grep -q "v$ver/$r" /etc/apk/repositories ||       echo "https://dl-cdn.alpinelinux.org/alpine/v$ver/$r" >> /etc/apk/repositories
  done
  # Bez RTC zegar startuje od zera (TLS do repozytoriow wymaga poprawnego czasu).
  rc-update add ntpd default 2>/dev/null

  mkdir -p /etc/network
  cat > /etc/network/interfaces <<'EOF'
auto lo
iface lo inet loopback

# Interfejs SRP (eth0) przygotowuje usluga srp-net, adres nadaje
# /srp/opt/cpu_srp/network_interface.sh.
EOF
  if [ ! -f /etc/wpa_supplicant/wpa_supplicant.conf ] && [ -f "$BOOT/wpa_supplicant.conf" ]; then
    mkdir -p /etc/wpa_supplicant
    cp "$BOOT/wpa_supplicant.conf" /etc/wpa_supplicant/wpa_supplicant.conf
    chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf
  fi
  if [ ! -s /root/.ssh/authorized_keys ] && [ -f "$BOOT/authorized_keys" ]; then
    mkdir -p /root/.ssh && chmod 700 /root/.ssh
    cp "$BOOT/authorized_keys" /root/.ssh/authorized_keys
    chmod 600 /root/.ssh/authorized_keys
  fi
}

apply_packages() {
  echo "=== Pakiety ==="
  # Te sa w repozytorium na karcie - instaluja sie bez sieci.
  apk add -q openssh wpa_supplicant e2fsprogs || warn "apk add (pakiety z karty) nie powiodlo sie"
  # Te wymagaja internetu - opcjonalne narzedzia diagnostyczne.
  # Biblioteki C++ dla binarek SRP (linkowanie dynamiczne) - bez nich nic nie wstanie.
  apk add -q libstdc++ libgcc 2>/dev/null || bad "brak libstdc++/libgcc (wymaga sieci) - binarki SRP nie uruchomia sie"
  apk add -q i2c-tools iproute2 2>/dev/null || warn "i2c-tools/iproute2 niedostepne (brak sieci lub zly zegar)"
  apk cache download >/dev/null 2>&1
  rc-update add networking boot 2>/dev/null
  # wpa_supplicant uruchamia srp-net (na interfejsie eth0), nie usluga systemowa.
  rc-update del wpa_supplicant boot 2>/dev/null
}

apply_usb_console() {
  [ "$USB_CONSOLE" -eq 1 ] || return 0
  echo "=== Konsola serwisowa na porcie USB (ttyGS0) ==="
  # Wymaga dtoverlay=dwc2,dr_mode=peripheral w usercfg.txt (port USB jako urzadzenie).
  for m in dwc2 g_serial; do
    grep -qxF "$m" /etc/modules || echo "$m" >> /etc/modules
  done
  grep -q '^ttyGS0::' /etc/inittab || echo 'ttyGS0::respawn:/sbin/getty -L 115200 ttyGS0 vt100' >> /etc/inittab
  grep -qx 'ttyGS0' /etc/securetty || echo 'ttyGS0' >> /etc/securetty
}

apply_tree() {
  echo "=== Katalogi ==="
  mkdir -p /srp/opt /srp/platform /srp/update/new /srp/update/backup /srp/update/current /srp/token
  mkdir -p /data /logs /home
  # FileLoggerApp pisze do "/home/root/" (sciezka z BBB) - kierujemy na karte,
  # dopoki sciezki nie przejmie platforma w kodzie.
  if mountpoint -q /data && [ ! -e /home/root ]; then ln -s /data /home/root; fi
}

apply_ssh() {
  echo "=== SSH root (flash_ecu uzywa ssh/scp root@IP) ==="
  mkdir -p /etc/ssh/sshd_config.d
  # Tylko klucz - bez pustego hasla roota jak w obrazie Yocto (debug-tweaks).
  printf 'PermitRootLogin prohibit-password\nUseDNS no\n' > /etc/ssh/sshd_config.d/10-srp.conf
  grep -q '^Include /etc/ssh/sshd_config.d/' /etc/ssh/sshd_config 2>/dev/null || \
    sed -i '1i Include /etc/ssh/sshd_config.d/*.conf' /etc/ssh/sshd_config
  rc-update add sshd default 2>/dev/null
  [ -s /root/.ssh/authorized_keys ] || warn "dodaj klucz publiczny do /root/.ssh/authorized_keys"
}

apply_hw() {
  echo "=== Sprzet: 1-Wire, I2C, UART ==="
  mount -o remount,rw "$BOOT" || die "nie moge przemontowac $BOOT do zapisu"
  [ -f "$USERCFG" ] && [ ! -f "$USERCFG.srp-orig" ] && cp "$USERCFG" "$USERCFG.srp-orig"
  touch "$USERCFG"
  set_usercfg_line "dtoverlay=w1-gpio,gpiopin=$W1_PIN"
  set_usercfg_line "dtparam=i2c_arm=on"
  set_usercfg_line "enable_uart=1"
  # PL011 (ttyAMA0) na GPIO14/15 zamiast mini-UART - stabilny baud dla radia.
  [ "$KEEP_BT" -eq 0 ] && set_usercfg_line "dtoverlay=disable-bt"
  [ "$USB_CONSOLE" -eq 1 ] && set_usercfg_line "dtoverlay=dwc2,dr_mode=peripheral"
  sed -i '/^gpu_mem=/d' "$USERCFG"
  # UART ma byc wolny dla aplikacji: bez konsoli kernela na porcie szeregowym.
  if grep -qE 'console=(serial0|ttyAMA0|ttyS0)' "$BOOT/cmdline.txt" 2>/dev/null; then
    cp -n "$BOOT/cmdline.txt" "$BOOT/cmdline.txt.srp-orig" 2>/dev/null
    sed -i -E 's/console=(serial0|ttyAMA0|ttyS0)(,[0-9]+)? ?//g' "$BOOT/cmdline.txt"
    info "usunieto konsole szeregowa z cmdline.txt"
  fi
  sync
  mount -o remount,ro "$BOOT"
  sed -i -E 's/^(ttyAMA0|ttyS0)::/#\1::/' /etc/inittab

  for m in i2c-dev w1-gpio w1-therm; do
    grep -qxF "$m" /etc/modules || echo "$m" >> /etc/modules
    modprobe "$m" 2>/dev/null
  done
}

apply_service() {
  echo "=== Start SRP przy boocie (OpenRC) ==="
  mkdir -p /usr/local/sbin
  cat > /usr/local/sbin/srp_start.sh <<'EOF'
#!/bin/sh
# Odpowiednik /etc/init.d/srp_start.sh z obrazu srp-poky (BBB).
token=/srp/token/token.hex
token_ver=/srp/token/token.hex.sha1

mkdir -p /srp/opt /srp/platform /srp/update/backup /srp/update/new /srp/update/current /srp/token

if [ ! -e "$token" ]; then
  mac="$(cat /sys/class/net/*/address | tr -d '\n')"
  echo "${mac}sec_token2" | sha256sum | cut -d' ' -f1 > "$token"
  echo "${mac}eng_token2" | sha256sum | cut -d' ' -f1 >> "$token"
  echo "${mac}upload_token2" | sha256sum | cut -d' ' -f1 >> "$token"
  sha1sum "$token" > "$token_ver"
  chmod -w "$token" "$token_ver"
fi

if sha1sum -c "$token_ver"; then
  if [ -f /srp/opt/cpu_srp/start_up.sh ]; then
    sh /srp/opt/cpu_srp/start_up.sh
  else
    echo "[SRP-PLATFORM]: [INFO] Brak oprogramowania w /srp/opt (uzyj flash_ecu)"
  fi
else
  echo "[SRP-PLATFORM]: [ERROR] Security file breach detected"
  rm -f "$token" "$token_ver"
fi
EOF
  chmod 755 /usr/local/sbin/srp_start.sh

  cat > /usr/local/sbin/srp_net.sh <<'EOF'
#!/bin/sh
# Przygotowuje interfejs SRP "eth0" (patrz opis w provision.sh).
WPA_CONF=/etc/wpa_supplicant/wpa_supplicant.conf
log() { echo "[SRP-NET]: $*"; }

if [ ! -e /sys/class/net/eth0 ]; then
  # Sterownik WiFi potrafi nie wstac przy starcie (blad ladowania firmware po
  # cieplym restarcie) - czekamy, a w razie potrzeby ladujemy go ponownie.
  try=0
  while [ ! -e /sys/class/net/wlan0 ] && [ $try -lt 3 ]; do
    i=0
    while [ ! -e /sys/class/net/wlan0 ] && [ $i -lt 8 ]; do sleep 1; i=$((i + 1)); done
    [ -e /sys/class/net/wlan0 ] && break
    log "brak wlan0 - ponowne ladowanie sterownika (proba $((try + 1)))"
    modprobe -r brcmfmac_wcc brcmfmac 2>/dev/null
    sleep 1
    modprobe brcmfmac
    try=$((try + 1))
  done

  if [ -e /sys/class/net/wlan0 ]; then
    ip link set wlan0 down
    ip link set wlan0 name eth0 && log "WiFi jako eth0"
  else
    # Staly adres MAC z numeru seryjnego plytki (token SRP liczy sie z adresow MAC).
    mac="$(awk '/^Serial/{print $3}' /proc/cpuinfo | tail -c 7 | sed 's/\(..\)\(..\)\(..\)/02:53:50:\1:\2:\3/')"
    modprobe dummy numdummies=1
    ip link set dummy0 address "$mac" 2>/dev/null
    ip link set dummy0 name eth0 && ip link set eth0 multicast on && log "atrapa eth0 (brak WiFi)"
  fi
fi
ip link set eth0 up

if [ -d /sys/class/net/eth0/wireless ] || [ -d /sys/class/net/eth0/phy80211 ]; then
  if [ -f "$WPA_CONF" ] && ! pidof wpa_supplicant >/dev/null; then
    wpa_supplicant -B -i eth0 -c "$WPA_CONF" -P /run/wpa_eth0.pid >/dev/null && log "wpa_supplicant uruchomiony"
  fi
  # Adres serwisowy z DHCP jako dodatkowy - nie rusza adresu SRP.
  pidof udhcpc >/dev/null || udhcpc -b -i eth0 -s /usr/local/sbin/srp_dhcp_alias.sh -p /run/udhcpc_eth0.pid >/dev/null 2>&1
fi
exit 0
EOF
  chmod 755 /usr/local/sbin/srp_net.sh

  cat > /usr/local/sbin/srp_dhcp_alias.sh <<'EOF'
#!/bin/sh
# Skrypt udhcpc: adres z DHCP trafia na etykiete <if>:svc, obok adresu SRP.
case "$1" in
  bound|renew)
    ip addr flush dev "$interface" label "$interface:svc" 2>/dev/null
    ip addr add "$ip/${mask:-24}" dev "$interface" label "$interface:svc"
    [ -n "$router" ] && ip route replace default via "${router%% *}" dev "$interface" metric 300
    if [ -n "$dns" ]; then
      : > /etc/resolv.conf
      for d in $dns; do echo "nameserver $d" >> /etc/resolv.conf; done
    fi
    ;;
  deconfig)
    ip addr flush dev "$interface" label "$interface:svc" 2>/dev/null
    ;;
esac
exit 0
EOF
  chmod 755 /usr/local/sbin/srp_dhcp_alias.sh

  cat > /etc/init.d/srp-net <<'EOF'
#!/sbin/openrc-run
description="SRP network interface (eth0 = WiFi or dummy)"

depend() {
  need localmount
  after modules hwdrivers networking
  before srp
}

start() {
  ebegin "Preparing SRP interface eth0"
  log=/var/log/srp_net.log
  mountpoint -q /logs && log=/logs/srp_net.log
  /usr/local/sbin/srp_net.sh > "$log" 2>&1
  eend $?
}

stop() {
  killall wpa_supplicant udhcpc 2>/dev/null
  return 0
}
EOF
  chmod 755 /etc/init.d/srp-net
  rc-update add srp-net default

  cat > /usr/local/sbin/srp_clock.sh <<'EOF'
#!/bin/sh
# Programowy zegar: plytka nie ma RTC, wiec czas jest okresowo zapisywany na karte.
F=/logs/.srp_clock
case "$1" in
  restore)
    [ -f "$F" ] || exit 0
    saved="$(cat "$F")"
    now="$(date +%s)"
    if [ "${saved:-0}" -gt "$now" ]; then date -s "@$saved" >/dev/null; fi
    ;;
  save)
    date +%s > "$F.tmp" && mv "$F.tmp" "$F"
    ;;
  loop)
    while :; do sleep 60; "$0" save; done
    ;;
esac
EOF
  chmod 755 /usr/local/sbin/srp_clock.sh

  cat > /etc/init.d/srp-clock <<'EOF'
#!/sbin/openrc-run
description="SRP software clock (saves time to /logs, restores at boot)"
command="/usr/local/sbin/srp_clock.sh"
command_args="loop"
command_background=true
pidfile="/run/srp-clock.pid"

depend() {
  need localmount
  before srp ntpd
}

start_pre() {
  /usr/local/sbin/srp_clock.sh restore
}

stop_post() {
  /usr/local/sbin/srp_clock.sh save
}
EOF
  chmod 755 /etc/init.d/srp-clock
  rc-update add srp-clock boot

  cat > /etc/init.d/srp <<'EOF'
#!/sbin/openrc-run
description="SRP platform start (em + state_manager)"

depend() {
  need localmount srp-net
  after modules
}

start() {
  ebegin "Starting SRP"
  log=/var/log/srp_start.log
  mountpoint -q /logs && log=/logs/srp_start.log
  /usr/local/sbin/srp_start.sh > "$log" 2>&1
  eend $?
}

stop() {
  ebegin "Stopping SRP"
  killall em state_manager 2>/dev/null
  eend 0
}
EOF
  chmod 755 /etc/init.d/srp
  rc-update add srp default
  rc-update add modules boot 2>/dev/null
}

apply_commit() {
  echo "=== Zapis konfiguracji (lbu) ==="
  # /srp (oprogramowanie) zyje w RAM - utrwalane razem z /etc przy `lbu commit`.
  lbu include /srp /root/.ssh /usr/local/sbin /etc/init.d/srp /etc/init.d/srp-net /etc/init.d/srp-clock /home/root 2>/dev/null
  if [ "$COMMIT" -eq 1 ]; then
    if lbu commit -d; then
      ok "lbu commit wykonany"
      # Nakladka pierwszego startu nie jest juz potrzebna (zostaje jako kopia awaryjna:
      # zmiana nazwy z powrotem przywraca dostep, gdyby system nie wstal).
      if [ -f "$BOOT/headless.apkovl.tar.gz" ] && [ -f "$BOOT/$(hostname).apkovl.tar.gz" ]; then
        mount -o remount,rw "$BOOT" && mv "$BOOT/headless.apkovl.tar.gz" "$BOOT/headless.apkovl.tar.gz.off"
        sync; mount -o remount,ro "$BOOT"
        info "headless.apkovl.tar.gz -> .off"
      fi
    else
      warn "lbu commit nie powiodl sie (setup-lbu?)"
    fi
  else
    warn "pominieto lbu commit - zmiany znikna po restarcie"
  fi
}

################################################################################
case "$MODE" in
  check) ;;
  partitions)
    [ "$(id -u)" -eq 0 ] || die "wymaga roota"
    do_partitions
    ;;
  apply)
    [ "$(id -u)" -eq 0 ] || die "wymaga roota"
    [ -f /etc/alpine-release ] || die "to nie Alpine Linux"
    apply_alpine
    apply_packages
    apply_usb_console
    apply_tree
    apply_ssh
    apply_hw
    apply_service
    apply_commit
    echo
    echo "Zmiany wprowadzone. Wymagany reboot (usercfg.txt, moduly)."
    echo
    ;;
  *) echo "Tryby: check | partitions [data] [logs] | apply"; exit 2 ;;
esac

check_system
exit $FAIL
