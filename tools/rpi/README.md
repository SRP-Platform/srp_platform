# Raspberry Pi Zero 2 W jako komputer SRP

Opis obrazu systemu dla RPi Zero 2 W, który zastępuje BeagleBone Black (BBB) jako komputer SRP (EC/FC/SEC_EC). Dokument wyjaśnia, jak obraz jest zbudowany, dlaczego tak, jak przygotować nową kartę, jak wgrywać oprogramowanie i jak diagnozować problemy.

Pliki w tym katalogu:

| Plik | Do czego |
|------|----------|
| `provision.sh` | Konfiguruje Alpine na RPi pod SRP (uruchamiany na RPi jako root) |
| `README.md` | Ten dokument |

---

## 1. Najważniejsze w skrócie

- System: **Alpine Linux aarch64** w trybie **diskless** — cały system działa z RAM, karta jest tylko czytana przy starcie.
- Oprogramowanie SRP budujesz jak dla BBB, tylko z inną platformą: `--config=rpi-platform`. Wgrywasz tym samym `flash_ec`.
- Konfiguracja SRP (IP, interfejs, aplikacje) jest **wspólna** dla BBB i RPi. Różnice sprzętowe rozwiązuje kod (`select` na `@srp_platform//bazel/platforms:is_rpi_zero2w`).
- Zmiany w systemie (pliki w `/etc`, oprogramowanie w `/srp`) są tracone przy restarcie, **chyba że** zrobisz `lbu commit`. `flash_ec` robi go sam.
- Dane z lotu i logi idą na osobne partycje karty (`/data`, `/logs`) i są zapisywane od razu.
- Sieć SRP to interfejs `eth0` — na RPi jest nim karta WiFi. Zerwanie WiFi nie psuje komunikacji lokalnej.

---

## 2. Dlaczego tak

### Alpine w trybie diskless

- **Odporność na zanik zasilania.** System plików w RAM, partycja systemowa zamontowana tylko do odczytu. Nagłe odcięcie zasilania (lądowanie, odłączenie baterii) nie uszkodzi systemu — przy następnym starcie wczytuje się ten sam, ostatnio zatwierdzony stan.
- **Powtarzalność.** Po każdym restarcie system jest dokładnie w stanie z ostatniego `lbu commit`. Przypadkowe zmiany „na żywo” znikają.
- **Mały i szybki.** Bazowy system zajmuje kilkadziesiąt MB RAM, start trwa kilkanaście sekund.

Koszt: wszystko, co ma przetrwać restart, trzeba świadomie zatwierdzić (`lbu commit`) albo zapisać na `/data` lub `/logs`.

### Biblioteka C: musl zamiast glibc

Alpine używa musl. Binarki z toolchainu BBB (glibc, armv7) się tu nie uruchomią, dlatego `rpi-platform` ma własny toolchain:

- **Bootlin `aarch64--musl--stable-2025.08-1`** (gcc 14.3), pobierany przez bazel (`third_party/toolchains/toolchains.bzl`).
- Binarki linkowane **dynamicznie** z bibliotekami systemu: musl, `libstdc++`, `libgcc` (pakiety Alpine). Pakiet EC ma ok. 10 MB; przy linkowaniu statycznym miał 37 MB, a każdy proces trzymał w RAM własną kopię bibliotek.
- Warunek zgodności: `libstdc++` w systemie co najmniej tak nowe jak w kompilatorze (gcc 14.3, `GLIBCXX_3.4.33`). Alpine 3.24 ma 15.2 — OK. Ponieważ obraz nie będzie aktualizowany, ten warunek jest stały.
- `libstdc++` z Alpine jest zbudowana bez wersji symboli (sprawdzone: 3.22 — gcc 14.2, `libstdc++.so.6.0.33`; 3.24 — gcc 15.2, `libstdc++.so.6.0.34`), więc ciągu `GLIBCXX_3.4.33` w pliku nie ma — `check` uznaje wtedy wersję z nazwy pliku (≥ `6.0.33` = gcc 14). Loader musl wersji symboli nie sprawdza.

Różnice musl, które wyszły w kodzie: brak makr glibc (`__attribute_noinline__`, `u_int32_t`), brak niejawnych include'ów (`pid_t` wymaga `<sys/types.h>`) i — najważniejsze — inne zachowanie zegarów (rozdział 8).

---

## 3. Układ karty

| Partycja | Rozmiar | System plików | Punkt montowania | Zawartość |
|----------|---------|---------------|------------------|-----------|
| p1 `PIBOOT` | 1 GB | FAT32 | `/media/mmcblk0p1` (tylko odczyt) | Bootloader, kernel, initramfs, moduły, repozytorium pakietów, zapisany stan systemu `srp-rpi.apkovl.tar.gz`, cache pakietów |
| p2 `srp-data` | 16 GB | ext4 | `/data` | Dane z lotu |
| p3 `srp-logs` | 8 GB | ext4 | `/logs` | Logi (w tym log startu SRP, zapis zegara) |

Razem ok. 25 GB — mieści się na karcie 32 GB; reszta karty zostaje wolna. Na karcie 64 GB można powiększyć `/data` (rozdział 9).

**Czy 1 GB partycji systemowej zużywa RAM?** Nie. Do RAM trafia tylko to, co system rozpakowuje: zainstalowane pakiety i zapisany stan (`/etc`, `/srp`). Kernel ładuje się raz, moduły są czytane wprost z karty, cache pakietów leży na karcie. Wolne miejsce na p1 to zapas (większe oprogramowanie, kopie stanu).

---

## 4. Jak system startuje

1. Firmware RPi czyta z p1 `config.txt` i `usercfg.txt`, ładuje kernel i initramfs.
2. Initramfs tworzy system plików w RAM, instaluje pakiety z listy `/etc/apk/world` (z cache na karcie, bez sieci) i rozpakowuje zapisany stan `srp-rpi.apkovl.tar.gz` — stąd biorą się `/etc` i `/srp`.
3. OpenRC uruchamia usługi. Kolejność istotna dla SRP:

| Usługa | Poziom | Co robi |
|--------|--------|---------|
| `modules` | boot | Ładuje moduły z `/etc/modules`: `i2c-dev`, `w1-gpio`, `w1-therm`, `dwc2`, `g_serial` |
| `localmount` | boot | Montuje `/data` i `/logs` (wpisy w `/etc/fstab` po etykietach) |
| `srp-clock` | boot | Odtwarza zegar z `/logs/.srp_clock`, potem co 60 s zapisuje bieżący czas |
| `srp-net` | default | Przygotowuje interfejs `eth0` (rozdział 6) |
| `srp` | default | Odpowiednik `srp_start.sh` z BBB: token, potem `/srp/opt/cpu_srp/start_up.sh` |
| `sshd`, `ntpd` | default | Dostęp ssh, synchronizacja czasu z internetu (gdy jest) |

4. `start_up.sh` (generowany przez bazel, ten sam co na BBB) ustawia adres SRP na `eth0` (`ifconfig`), uruchamia `em` i `state_manager`, a te — aplikacje.

Logi startu: `/logs/srp_net.log` (sieć) i `/logs/srp_start.log` (SRP, łącznie z wyjściem `em`).

### Odpowiedniki z obrazu BBB (Yocto)

| BBB (srp-poky) | RPi |
|----------------|-----|
| `/etc/init.d/srp_start.sh` (sysvinit) | usługa OpenRC `srp` → `/usr/local/sbin/srp_start.sh` |
| `/srp/{opt,platform,update,token}` | to samo, w RAM, utrwalane przez `lbu` |
| `/home/root/` (logi FileLoggerApp) | dowiązanie `/home/root → /data` |
| root po ssh bez hasła | root po ssh **tylko kluczem** (gość QEMU `--qemu`: bez hasła, jak BBB) |
| `eth0` (Ethernet) | `eth0` = karta WiFi albo atrapa (rozdział 6) |
| `/dev/i2c-2`, `/dev/ttyS1` | `/dev/i2c-1`, `/dev/ttyAMA0` — **nie** mapowane; obsługa w kodzie platformą |

---

## 5. Sprzęt

Ustawiane w `usercfg.txt` na p1 (przez `provision.sh apply`):

| Linia | Skutek |
|-------|--------|
| `dtoverlay=dwc2,dr_mode=peripheral` | Port USB (ten oznaczony USB, nie PWR) działa jako urządzenie: konsola szeregowa dla laptopa. Nie podłączysz do niego wtedy huba ani adaptera USB |
| `dtoverlay=w1-gpio,gpiopin=4` | 1-Wire na GPIO4 (pin 7); czujniki w `/sys/bus/w1/devices/` |
| `dtparam=i2c_arm=on` | I2C na GPIO2/3 → `/dev/i2c-1` |
| `enable_uart=1`, `dtoverlay=disable-bt` | Pełny UART PL011 na GPIO14/15 → `/dev/ttyAMA0`; Bluetooth wyłączony |

GPIO: sysfs (`/sys/class/gpio`) działa, baza kontrolera to **512**, więc numer w konfiguracji = 512 + numer BCM (np. GPIO17 → 529). Mapę pinów EC trzeba przygotować dla RPi (obecna jest dla BBB).

Brak RTC — rozdział 8.

---

## 6. Sieć

### Jak to działa

Wspólna konfiguracja SRP (`platform_config.json` i inne) i DoIP mają na sztywno interfejs `eth0` i adres `192.168.10.51`. Zero 2 W nie ma Ethernetu, więc usługa `srp-net` przy starcie:

1. Czeka na kartę WiFi (`wlan0`). Gdy sterownik nie wstanie (zdarza się po ciepłym restarcie — błąd ładowania firmware), ładuje go ponownie, do 3 prób.
2. **Zmienia nazwę `wlan0` na `eth0`** i uruchamia na nim `wpa_supplicant` (sieć z `/etc/wpa_supplicant/wpa_supplicant.conf`).
3. Uruchamia DHCP, który dodaje adres jako **dodatkowy** (etykieta `eth0:svc`) — do serwisu i internetu. Adres SRP nadaje później `start_up.sh`.
4. Gdy karty WiFi nie ma wcale, tworzy atrapę `eth0` (moduł `dummy`) ze stałym adresem MAC z numeru seryjnego płytki.
5. Gdy jest prawdziwy `eth0` (np. adapter USB-Ethernet), nic nie zmienia.

Dlaczego karta WiFi jako `eth0`, a nie atrapa z przekazywaniem ruchu: kod SRP przypina multicast (SOME/IP SD, czas, DLT) do interfejsu, który ma adres SRP. Z atrapy multicast nie wyszedłby na WiFi bez demona routingu multicast, którego w Alpine 3.24 nie ma.

### Zerwanie WiFi

Sprawdzone: po utracie połączenia interfejs `eth0` zostaje w stanie UP z adresem 192.168.10.51; komunikacja lokalna (multicast SOME/IP, unicast/TCP na adres SRP) działa dalej. `wpa_supplicant` sam łączy się ponownie, gdy sieć wróci.

### Adresy

| Adres | Skąd | Do czego |
|-------|------|----------|
| `192.168.10.51/24` | `start_up.sh` z `platform_config.json` | SRP (SOME/IP, DLT, DoIP, `flash_ec`) |
| adres z DHCP (np. `10.115.245.x`) | hotspot / sieć WiFi | serwis, internet, `apk` |

W jednej sieci nie może być drugiego urządzenia z `192.168.10.51` (np. EC na BBB).

### Zmiana sieci WiFi

```sh
nano /etc/wpa_supplicant/wpa_supplicant.conf   # ssid / psk
lbu commit -d
reboot
```

Uwaga: plik `wpa_supplicant.conf` na partycji p1 (z pierwszego startu) zawiera hasło jawnym tekstem — usuń go, jeśli karta trafia w obce ręce (`mount -o remount,rw /media/mmcblk0p1; rm /media/mmcblk0p1/wpa_supplicant.conf; mount -o remount,ro /media/mmcblk0p1`).

---

## 7. Dostęp do RPi

### SSH (laptop w tej samej sieci WiFi)

Root loguje się **tylko kluczem** (`/root/.ssh/authorized_keys`), bez hasła.

```sh
ssh root@192.168.10.51          # gdy laptop ma trasę do 192.168.10.0/24
ssh root@<adres z DHCP>         # zawsze działa w tej samej sieci
```

Jeśli laptop jest w innej podsieci niż 192.168.10.x (np. hotspot 10.115.245.x), dodaj trasę w Windows (PowerShell jako administrator; znika po restarcie laptopa):

```powershell
route add 192.168.10.51 mask 255.255.255.255 <adres RPi z DHCP>
```

Adres z DHCP odczytasz z panelu hotspotu albo przez konsolę szeregową (`ip addr`).

### Konsola szeregowa przez USB (bez sieci)

Kabel do portu **USB** (nie PWR). W Windows pojawia się port COM („Urządzenie szeregowe USB”). Terminal (PuTTY, Tera Term): 115200, 8N1. Login: `root` (bez hasła — dostęp fizyczny).

### Podgląd zasobów

```sh
htop                     # procesy, CPU, RAM
free -m                  # RAM; kolumna available = realnie wolne
ps -o pid,rss,args | grep /srp/          # RAM procesów SRP (kB)
df -h / /data /logs /media/mmcblk0p1     # RAM-owy system plików i partycje
cat /logs/srp_start.log                  # start SRP
rc-status                                # stan usług
```

Stan wyjściowy (EC, 15 procesów SRP): ok. 52 MB RAM zajęte + 46 MB systemu plików w RAM, ok. 300 MB dostępne z 416 MB.

---

## 8. Czas i zegary

### Zegar programowy (`srp-clock`)

Płytka nie ma RTC — po restarcie kernel startuje z datą z initramfs. Usługa `srp-clock`:

- przy starcie odtwarza czas z `/logs/.srp_clock` (tylko do przodu — nigdy nie cofa zegara),
- co 60 s zapisuje bieżący czas na kartę.

Czas po restarcie jest więc „ostatni zapisany” — gubi czas postoju bez zasilania (+ do 60 s). Gdy jest internet, `ntpd` go poprawia. Dla wiarygodnego czasu bez sieci potrzebny jest moduł RTC na I2C (np. DS3231) albo GPS z PPS.

### Korekty czasu a kod SRP

Skok zegara systemowego (NTP, GPS, ręczne `date`) jest groźny dla kodu, który odmierza odstępy zegarem ściennym:

1. **`system_clock` / `high_resolution_clock`** (w libstdc++ to ten sam zegar) do timeoutów i watchdogów — poprawione w `srp`: branch `steady-clock-intervals` (zamiana na `steady_clock`).
2. **`condition_variable::wait_for` na musl.** libstdc++ odmierza limit zegarem monotonicznym tylko z glibc (`pthread_cond_clockwait`); na musl używa zegara ściennego. Zmierzone na RPi: oczekiwanie 4 s po cofnięciu zegara o 20 s trwało 25 s. Dotyczy `core::condition::wait_for` (większość pętli w platformie i w `srp`) oraz bezpośrednich `cv.wait_for` (np. `refuel_controller.cpp`).
   - Poprawione w `srp_platform`: branch `monotonic-condition-wait` (`std::binary_semaphore` zamiast zmiennej warunkowej — jego timeout libstdc++ odmierza zegarem monotonicznym niezależnie od biblioteki C). Po poprawce: 4000 ms przy cofnięciu i przy skoku.
   - **`srp` ma własną kopię `core/common/condition.h`** — wymaga tej samej poprawki; `refuel_controller.cpp` osobnej.

`sleep_for`, `steady_clock` i timeouty gniazd są odporne na skoki.

---

## 9. Przygotowanie nowej karty od zera

Potrzebne: karta 32/64 GB, laptop z Windows, kabel micro-USB do portu USB RPi, dostęp do sieci WiFi z internetem (pierwsza konfiguracja pobiera kilka pakietów).

### 9.1. Na laptopie

1. Pobierz Alpine dla RPi: `alpine-rpi-3.24.x-aarch64.tar.gz` (alpinelinux.org → Downloads → Raspberry Pi).
2. Na karcie załóż **jedną partycję FAT32 1 GB** (MBR, aktywna), etykieta `PIBOOT`; reszta karty nieprzydzielona. (Zarządzanie dyskami Windows, `diskpart` albo Rufus; Windows domyślnie formatuje cały nośnik — trzeba wskazać rozmiar 1024 MB.)
3. Rozpakuj archiwum Alpine na tę partycję (zawartość, nie katalog).
4. Dodaj na partycję pliki pierwszego startu:

| Plik | Zawartość |
|------|-----------|
| `headless.apkovl.tar.gz` | Nakładka pierwszego startu bez monitora (projekt `macmpi/alpine-linux-headless-bootstrap`): wystawia ssh i konsolę USB |
| `authorized_keys` | Klucz(e) publiczne, które mają logować się jako root |
| `wpa_supplicant.conf` | Sieć WiFi (format poniżej, końce linii LF) |
| `usercfg.txt` | `dtoverlay=dwc2,dr_mode=peripheral` |
| `opt-out` | Pusty plik — wyłącza telemetrię i auto-aktualizację nakładki |

```
country=PL

network={
	key_mgmt=WPA-PSK
	ssid="NAZWA_SIECI"
	psk="HASLO"
}
```

5. Wysuń kartę bezpiecznie, włóż do RPi, kabel do portu USB laptopa, zasilanie.

### 9.2. Na RPi (konsola USB albo ssh)

Skopiuj `provision.sh` na RPi (`scp provision.sh root@<adres>:/root/` albo przez konsolę), potem:

```sh
date -s "2026-11-16 12:00:00"      # bez RTC: TLS do repozytoriów wymaga poprawnej daty
sh /root/provision.sh check         # tylko sprawdza
sh /root/provision.sh partitions    # /data 16G, /logs 8G (np.: partitions 40G 16G na karcie 64 GB)
sh /root/provision.sh apply         # konfiguracja + lbu commit
reboot
sh /root/provision.sh check         # po restarcie wszystko [ OK ]
```

Jeżeli Windows widzi partycję karty przez USB (urządzenie pamięci masowej „Linux File-Stor Gadget”), **wysuń ją w Windows przed `partitions`/`apply`** — dwa systemy nie mogą jednocześnie pisać do tej samej partycji FAT.

Po pierwszym `lbu commit` system startuje z własnego stanu (`srp-rpi.apkovl.tar.gz`) i nakładka pierwszego startu nie jest już używana (na obecnej karcie jej nie ma). Gdyby system przestał być osiągalny, wgraj ją ponownie na p1 i usuń/przemianuj `srp-rpi.apkovl.tar.gz` — dostaniesz znów dostęp jak przy pierwszym starcie.

### 9.3. Pierwszy flash

Z WSL, gdy RPi jest osiągalne pod `192.168.10.51` (rozdział 7):

```sh
bazel run --config=rpi-platform //deployment/tools/ecu:flash_ec
```

### Co robi `provision.sh`

| Tryb | Działanie |
|------|-----------|
| `check` | Sprawdza: wersję Alpine, architekturę, tryb diskless, cache pakietów, `/data`, `/logs`, `/srp` w `lbu`, GPIO, I2C, UART, 1-Wire, biblioteki C++, ssh, usługi, `eth0`, zegar. Nic nie zmienia |
| `partitions [data] [logs]` | Dopisuje p2 i p3 w wolnym miejscu karty (istniejących partycji nie rusza; partycji z innym systemem plików nie nadpisze), formatuje ext4 z etykietami, wpisuje do `fstab`, montuje. Kopia tablicy partycji: `/root/partition-table.before.txt` |
| `apply` | Hostname `srp-rpi`; `setup-lbu` i cache pakietów na p1; repozytoria sieciowe; pakiety (`openssh`, `wpa_supplicant`, `e2fsprogs`, `libstdc++`, `libgcc`, `i2c-tools`, `iproute2`); konsola USB; katalogi `/srp`, `/data`, `/logs`, `/home/root`; ssh root tylko kluczem; `usercfg.txt`; usługi `srp-net`, `srp-clock`, `srp`; `lbu commit` |

Opcje `apply`: `--w1-pin <bcm>`, `--keep-bt`, `--hostname <nazwa>`, `--no-usb-console`, `--no-commit`, `--qemu`. Skrypt jest idempotentny — można go uruchamiać wielokrotnie.

### Tryb `--qemu` (emulator, bez płytki)

`apply --qemu` przygotowuje tę samą część programową dla gościa QEMU (maszyna `virt`, Alpine w trybie `sys` na dysku virtio) — używa go builder obrazu `@srp_platform//tools/qemu:build_rpi_image` (opis: `tools/qemu/README.md`, cel `:qemu_rpi`).

| Robi | Pomija |
|------|--------|
| hostname, repozytoria sieciowe, pakiety `openssh libstdc++ libgcc`, `/srp`, `/data`, `/logs`, `/home/root → /data`, ssh root tylko kluczem, usługi `srp` i `srp-clock`, `lbu commit` | `setup-lbu`/cache na karcie, `ntpd`, WiFi i `srp-net`, `partitions`, konsola USB `ttyGS0`, `usercfg.txt`, 1-Wire/I2C/UART |

Różnice: `eth0` to karta virtio ze stałym adresem `192.168.10.101/24` (jak gość BBB; usługa `networking`, `srp` zależy od `net` zamiast `srp-net`); `lbu` zapisuje kopię stanu do katalogu `/var/lib/srp-lbu` (`LBU_BACKUPDIR` w `/etc/lbu/lbu.conf`), więc `lbu commit -d` z `flash_ecu` kończy się sukcesem, choć w trybie `sys` pliki i tak są trwałe.

`check` w gościu QEMU: jedyny dozwolony `[FAIL]` to `brak /sys/class/gpio` (jądro `virt` bez `CONFIG_GPIO_SYSFS`, maszyna bez GPIO płytki). `[WARN]` o trybie diskless, cache apk, partycjach `/data` i `/logs`, 1-Wire i zegarze programowym są oczekiwane.

---

## 10. Budowanie i wgrywanie oprogramowania

```sh
# budowa
bazel build --config=rpi-platform //deployment/cpu/ec:pkg
# budowa + wgranie
bazel run --config=rpi-platform //deployment/tools/ecu:flash_ec
```

Dopóki zmiany platformy nie są w wydaniu `srp_platform` na artifactory, dodaj:

```sh
--override_repository=srp_platform=<ścieżka do klonu srp_platform>
```

W samej platformie odpowiednikiem jest przykładowy ECU: `bazel build --config=rpi-platform //test_cpu/cpu/ec:pkg`, wgranie `bazel run --config=rpi-platform //test_cpu/cpu/ec:flash`.

`flash_ec` na RPi robi to samo co na BBB (usuwa `/srp/opt`, `/srp/platform`, kopiuje i rozpakowuje pakiet), a przed restartem dodatkowo `lbu commit -d` — inaczej nowe oprogramowanie zniknęłoby po restarcie. Różnica jest wybierana platformą (domyślny `post_install_cmd` w `tools/common/flash_ecu.bzl`, `select` na `is_rpi_zero2w`).

Elementy platformy RPi w repo:

| Miejsce | Co |
|---------|----|
| `.bazelrc` | `build:rpi-platform` |
| `bazel/platforms/BUILD` | platforma `rpi_zero2w` (aarch64, linux, `board_rpi_zero2w`) i `config_setting` `is_rpi_zero2w` / `is_bbb` — do `select()` w kodzie zależnym od sprzętu |
| `bazel/toolchain/aarch64-linux-musl/` | toolchain i wrapper (omija wrapper buildroota, który podaje sysroot ścieżką bezwzględną — bazel tego nie akceptuje) |
| `third_party/toolchains/` | pobieranie toolchainu Bootlin |
| `tools/common/flash_ecu.bzl` | parametr `post_install_cmd` (domyślnie `lbu commit -d` na RPi) |
| `tools/rpi/` | `provision.sh` i ten opis |

---

## 11. Utrwalanie zmian (`lbu`)

Wszystko w RAM ginie przy restarcie, poza tym, co zapisane przez `lbu commit` (`/etc` + ścieżki z `lbu include -l`: `/srp`, `/root/.ssh`, `/usr/local/sbin`, usługi SRP, `/home/root`) i tym, co na `/data` i `/logs`.

```sh
lbu status        # co zmieniło się od ostatniego commit
lbu diff          # szczegóły
lbu commit -d     # zapis na kartę (-d: usuń stare kopie)
```

Pakiety: `apk add <pakiet>` + `lbu commit` — przy starcie instalują się z cache na karcie, bez sieci. Narzędzia diagnostyczne (`htop`, `nano`, `tcpdump`) zwiększają obraz; jeśli mają nie trafić do obrazu docelowego, usuń je (`apk del`) przed `lbu commit`.

**Uwaga:** `flash_ec` robi `lbu commit`, więc utrwala też wszystkie Twoje niezatwierdzone zmiany w `/etc`.

---

## 12. Logi DLT na laptopie

Logger platformy wysyła DLT jako UDP **multicast `231.255.42.99:3490`** z adresu SRP (`eth0`, czyli po WiFi), ok. 140 ramek/s przy starcie. ECU ID: `EC--`.

W DLT Viewer:

1. *Config → ECU Add*: ECU ID `EC--`, połączenie **UDP**, port **3490**.
2. Zaznacz odbiór **multicast**, adres grupy `231.255.42.99`, interfejs — adres IP karty WiFi laptopa w tej samej sieci co RPi.
3. *Connect*.

Jeśli nic nie przychodzi:

- Zapora Windows: sieć hotspotu jest zwykle profilem „Publiczna” — zezwól DLT Viewer na ruch przychodzący UDP w tym profilu.
- Hotspot w telefonie może nie przekazywać multicastu między klientami — sprawdź na zwykłym routerze.
- Na RPi: `tcpdump -ni eth0 udp port 3490` (pakiet `tcpdump`, nie utrwalaj w obrazie) pokazuje, czy ramki wychodzą.

---

## 13. Znane problemy i otwarte sprawy

| Sprawa | Stan |
|--------|------|
| `servoService`, `RadioApp` nie startują | Oczekują `/dev/i2c-2`, `/dev/ttyS1` z BBB — do rozwiązania platformą w kodzie |
| Mapa GPIO EC | Numery dla BBB; dla RPi: 512 + BCM |
| Sterownik WiFi czasem nie wstaje po ciepłym restarcie | `srp-net` ładuje go ponownie (do 3 prób); obserwować |
| `wait_for` na musl liczy limit zegarem ściennym | Poprawione w `srp_platform` (`monotonic-condition-wait`); `srp` wymaga tej samej poprawki we własnej kopii `core/common/condition.h` i w `refuel_controller.cpp` |
| Odmierzanie interwałów `system_clock` | Poprawione w `srp` (`steady-clock-intervals`) |
| Wydanie `srp_platform` | Zmiany lokalne — build wymaga `--override_repository` |
| Brak RTC | Zegar programowy; dla czasu bez sieci: RTC na I2C albo GPS + PPS |
| GPS (FC) | Brak drugiego UART — planowany SC16IS752 |
