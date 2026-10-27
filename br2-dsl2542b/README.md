# DSL-2542B + Buildroot: sniffer z port mirroringiem

Własny, minimalny Linux dla D-Link DSL-2542B (SoC BCM6338, switch BCM5325E,
16 MB RAM, 4 MB NOR), który po starcie sam włącza port mirroring.

Etap 1 (ten): system startuje **z RAM-u przez TFTP**, flash zostaje nietknięty.
Etap 2 (później): zapis do flasha.

---

## 1. Co jest w tym repozytorium

```
br2-dsl2542b/
├── external.desc            nazwa drzewa: DSL2542B -> zmienna $(BR2_EXTERNAL_DSL2542B_PATH)
├── external.mk              dołącza pakiety z package/*/*.mk
├── Config.in                dodaje nasze pakiety do menuconfig
├── configs/
│   └── dsl2542b_defconfig   cała konfiguracja Buildroota dla tej płytki
├── board/dlink/dsl2542b/
│   ├── linux.fragment       różnice względem bcm63xx_defconfig z kernela
│   ├── post-image.sh        robi z kernela plik dla CFE i sprawdza mapę pamięci
│   ├── lzma-loader/         mały program ładujący kernel (z OpenWrt, GPL-2.0)
│   └── rootfs-overlay/      pliki nadpisywane w rootfs (adres IP)
└── package/swmirror/        nasz pakiet: narzędzie + skrypt startowy
```

To jest **drzewo zewnętrzne** (*br2-external*). Buildroot pozostaje
nietknięty (jest submodułem repo `eth_sniffer`), a wszystko, co dotyczy tej
płytki, leży obok niego. Buildroot wczytuje to drzewo przez `BR2_EXTERNAL=...`
i zapamiętuje ścieżkę w `output/`, więc później wystarcza samo `make`
w katalogu `output/` (Buildroot tworzy tam mały Makefile przekierowujący).

---

## 2. Buildroot w pigułce

Buildroot to zestaw Makefile'i, który z samych źródeł buduje kompletny system:

```
 1. toolchain       binutils + gcc + uClibc-ng dla MIPS (output/host/)
 2. pakiety         busybox, swmirror, ...   -> output/target/  (przyszły rootfs)
 3. rootfs          output/target/ -> rootfs.cpio
 4. kernel          z rootfs.cpio wbudowanym jako initramfs -> vmlinux.bin
 5. post-image.sh   vmlinux.bin -> dsl2542b-ramboot.elf      (output/images/)
```

Najważniejsze katalogi w `output/`:

| Katalog | Zawartość |
|---|---|
| `build/<pakiet>-<wersja>/` | rozpakowane i zbudowane źródła każdego pakietu |
| `host/` | narzędzia działające na twoim PC, w tym cross-kompilator `mips-buildroot-linux-uclibc-gcc` |
| `staging/` | sysroot: nagłówki i biblioteki dla cross-kompilacji |
| `target/` | to, co trafi do rootfs routera (bez nagłówków, binarki bez symboli) |
| `images/` | gotowe obrazy |

Konfiguracja:

- `configs/dsl2542b_defconfig` to *minimalna* konfiguracja: tylko to, co
  różni się od wartości domyślnych.
- `make dsl2542b_defconfig` rozwija ją do pełnego `output/.config`.
- `make menuconfig` zmienia `.config`, a `make savedefconfig` zapisuje
  zmiany z powrotem do pliku w `configs/`.
- Kernel konfiguruje się tak samo, ale osobno: `bcm63xx_defconfig`
  z kernela + nasz `linux.fragment`. Podgląd przez `make linux-menuconfig`.

Buildroot **nie śledzi zależności między konfiguracją a już zbudowanymi
pakietami**. Po zmianie pakietu robisz `make <pakiet>-rebuild`, a po zmianie
architektury albo toolchaina `make clean` i pełne budowanie od nowa.

---

## 3. Budowanie (WSL / Linux)

Pakiety systemowe (Ubuntu/Debian):

```sh
sudo apt install build-essential git rsync bc cpio unzip file wget \
                 perl python3 libncurses-dev xz-utils
```

**Buduj w systemie plików Linuksa** (np. `~/eth_sniffer`), nie na `/mnt/c/...`.
Na dyskach Windowsa build jest wielokrotnie wolniejszy i potrafi się wyłożyć
na rozróżnianiu wielkości liter.

```sh
# Buildroot jest submodułem repo eth_sniffer (przypięty do tagu 2026.02.3).
cd ~/eth_sniffer
git submodule update --init
# O= : wyniki builda w ~/eth_sniffer/output, poza submodułem Buildroota
make -C buildroot O=$PWD/output BR2_EXTERNAL=$PWD/br2-dsl2542b dsl2542b_defconfig
cd output
make 2>&1 | tee build.log
```

Pierwsze budowanie trwa od 30 do 60 minut, bo powstaje cały toolchain.
Następne są szybkie. Na końcu post-image.sh wypisuje mapę pamięci:

```
=== Obraz do uruchomienia z RAM przez CFE ===
  plik:                  .../output/images/dsl2542b-ramboot.elf
  ...
```

Jeśli którakolwiek kontrola się nie powiedzie, skrypt wypisze `BŁĄD:`
i build zakończy się błędem. To celowe, bo taki obraz zawiesiłby CFE.

Przydatne polecenia (wykonywane w katalogu `output/`):

| Polecenie | Co robi |
|---|---|
| `make swmirror-rebuild all` | przebudowuje nasz pakiet i obrazy |
| `make linux-rebuild all` | przebudowuje kernel |
| `make linux-menuconfig` | konfiguracja kernela (zmiany przenieś do `linux.fragment`) |
| `make busybox-menuconfig` | wybór apletów BusyBoxa |
| `make savedefconfig` | zapisuje `.config` z powrotem do `configs/dsl2542b_defconfig` |

---

## 4. Mapa pamięci, czyli po co jest lzma-loader

CFE ładuje przez TFTP plik ELF i uruchamia go. Ma przy tym jedno twarde
ograniczenie: **samo CFE siedzi w RAM-ie** pod adresami `0x80401000–0x80528000`
(widać to w logu startowym: *Total memory used by CFE*). Plik, którego segmenty
wypadają w tym obszarze, nadpisze CFE w trakcie ładowania.

Kernel 6.12 z initramfs po rozpakowaniu zajmuje kilka MB, a ma działać od
`0x80010000`, więc bezpośrednio się nie zmieści. Rozwiązanie z OpenWrt:

```
 Krok 1: CFE ładuje loader.elf (mały: kod + kernel skompresowany LZMA)
   0x80010000 [loader + kernel.lzma]          0x80401000 [CFE ........]

 Krok 2: loader kopiuje się wyżej (head.S)
   0x80010000 [stara kopia]   0x80a00000 [loader + kernel.lzma]

 Krok 3: loader rozpakowuje kernel na właściwe miejsce i do niego skacze
   0x80010000 [kernel ............]           0x80a00000 [loader]
```

Po kroku 3 CFE i loader nie są już potrzebne, więc kernel używa całego RAM-u.
`post-image.sh` sprawdza wszystkie trzy warunki:

1. loader.elf kończy się przed `0x80401000`,
2. rozpakowany kernel (łącznie z `.bss`, czyli symbolem `_end`) kończy się przed `0x80a00000`,
3. loader po przeniesieniu mieści się w 16 MB.

Kompresja: `xz --format=lzma` tworzy nagłówek z rozmiarem „nieznany”,
a dekoder w loaderze potrzebuje dokładnego rozmiaru wyjścia, więc skrypt wpisuje
go do nagłówka (bajty 5–12, little-endian).

---

## 5. Uruchomienie z RAM-u przez CFE (bez flashowania)

### Serwer TFTP

Najprościej postawić go na Raspberry Pi w sieci LAN routera:

```sh
sudo apt install tftpd-hpa             # katalog: /srv/tftp
scp output/images/dsl2542b-ramboot.elf pi@192.168.1.5:/tmp/
ssh pi@192.168.1.5 sudo cp /tmp/dsl2542b-ramboot.elf /srv/tftp/
```

Serwer w WSL2 domyślnie siedzi za NAT-em Windowsa i router się do niego nie
dostanie. Jeśli chcesz serwować z laptopa, użyj programu pod Windowsem
(np. Tftpd64) albo włącz w WSL tryb sieci „mirrored”.

### Ustawienia CFE

Na konsoli UART przerwij autostart dowolnym klawiszem i wpisz `c`:

```
Board IP address                  : 192.168.1.1:ffffff00
Host IP address                   : 192.168.1.5          <- serwer TFTP
Run from flash/host (f/h)         : h
Default host run file name        : dsl2542b-ramboot.elf
```

Pozostałe pola zostaw bez zmian (Enter). Potem `r`.

Flaga `h` zostaje zapisana w NVRAM, więc po każdym włączeniu zasilania router
będzie startował twój obraz z TFTP. Przy pracy nad systemem to wygodne,
bo po przebudowaniu wystarczy podmienić plik i zrestartować router. Żeby wrócić
do oryginalnego firmware'u, ustaw z powrotem `f`.

### Czego się spodziewać na UART

```
OpenWrt kernel loader for BCM63XX
...
Decompressing kernel... done!
Starting kernel at 80010000...

Linux version 6.12.91 ...
CFE version: 1.0.37-8.7
...
bcm63xx_enet bcm63xx_enet.0 eth0: ...
...
DSL-2542B / Buildroot
dsl2542b login: root
# swmirror status
```

Logowanie: `root`, bez hasła.

---

## 6. swmirror

```sh
swmirror status                  # link portów i stan mirroringu
swmirror on lan4 lan1            # ruch z LAN1 (oba kierunki) -> LAN4
swmirror on lan4 lan1,lan2 rx    # tylko ruch odbierany na LAN1 i LAN2
swmirror on lan4 cpu             # cały ruch procesora routera
swmirror off
swmirror read 0x02 0x10          # surowy rejestr: strona, adres
swmirror write 0x02 0x10 0x8001
```

Przy starcie mirroring włącza `/etc/init.d/S41swmirror` według
`/etc/swmirror.conf` (domyślnie LAN1 -> LAN4). Plik domyślny jest w
`package/swmirror/swmirror.conf`.

Narzędzie robi dokładnie to samo, co wcześniej `setmem`, tylko przez
sterownik: ioctl `SIOCGMIIREG`/`SIOCSMIIREG` na `eth0` z adresem PHY 30.
Sterownik `bcm63xx_enet` dla płytki 96338W nie ma podpiętego PHY, więc
przekazuje dowolny adres PHY prosto na magistralę MDIO. W przeciwieństwie do
stockowego firmware'u nic nie odpytuje MDIO w tle, więc nie ma wyścigu
o rejestr.

Mapowanie portów: LAN1 = port 3 i LAN4 = port 0 sprawdzone na sprzęcie,
LAN2 = 2 i LAN3 = 1 wzięte z konfiguracji OpenWrt dla 96338W.

---

## 7. Gdy coś nie działa

| Objaw | Co sprawdzić |
|---|---|
| CFE nie pobiera pliku | Host IP i nazwa pliku w `c`, czy serwer TFTP odpowiada (`tftp` z innej maszyny), czy kabel jest w porcie LAN |
| CFE pobiera, ale odrzuca plik | Wklej dokładny komunikat. Jest szansa, że ta wersja CFE oczekuje innego formatu niż ELF |
| Po `Starting kernel` cisza | Kernel startuje, ale nie ma konsoli. Sprawdź `CONFIG_CMDLINE` i `CONFIG_SERIAL_BCM63XX_CONSOLE` w `make linux-menuconfig` |
| `unknown bcm963xx board` | Kernel nie rozpoznał Board ID z NVRAM, który powinien brzmieć `96338W` (polecenie `p` w CFE) |
| Brak sieci po starcie | Switch jest skonfigurowany przez CFE, kernel go nie rusza. Sprawdź `swmirror status` i `ip addr` |
| `swmirror`: `Operation not supported` | ioctl nie dotarł do bcm63xx_enet. Sprawdź, czy `eth0` istnieje (`ip link`) |

---

## 8. Co dalej

- **Flash:** obraz z nagłówkiem imagetag (Board ID `6338W`), wgrywany przez `f`
  w CFE albo przez web recovery. Kopia zapasowa oryginalnego flasha jest już zrobiona.
- **Dostęp po sieci:** `telnetd` z BusyBoxa albo `dropbear` (SSH).
- **Ćwiczenie z LED:** `ls /sys/class/leds/` i
  `echo heartbeat > /sys/class/leds/<dioda>/trigger`.
