# eth_sniffer: D-Link DSL-2542B jako sprzętowy sniffer Ethernet

Stary router ADSL (SoC BCM6338, switch BCM5325E) z własnym Linuksem
z Buildroota. Po starcie system sam włącza w switchu port mirroring:
ruch z wybranego portu LAN jest kopiowany na port z laptopem i Wiresharkiem.

## Zawartość

```
eth_sniffer/
├── buildroot/       submoduł: Buildroot 2026.02.3 (bez zmian)
├── br2-dsl2542b/    drzewo br2-external: defconfig, kernel, loader CFE, swmirror
└── dump_flash.py    zrzut flasha przez UART (dumpmem w stockowym firmware)
```

## Budowanie

```sh
git clone --recurse-submodules https://github.com/khirsz/eth_sniffer.git
cd eth_sniffer
make -C buildroot O=$PWD/output BR2_EXTERNAL=$PWD/br2-dsl2542b dsl2542b_defconfig
cd output
make 2>&1 | tee build.log
```

Wynik: `output/images/dsl2542b-ramboot.elf` (start z RAM przez TFTP z CFE).

### W kontenerze (Dev Containers)

`.devcontainer/` opisuje obraz Debiana 13 z pakietami, których Buildroot
wymaga od hosta. Cross-toolchain i tak buduje sam Buildroot.
VS Code: *Dev Containers: Reopen in Container*, a potem w terminalu
kontenera te same komendy co wyżej.

- Repo jest montowane pod **tą samą ścieżką** co na hoście, bo Buildroot
  wpisuje ścieżki absolutne do `output/`. Dzięki temu `output/` działa
  po obu stronach.
- Pobrane źródła trafiają do wolumenu `dsl2542b-br-dl` (`BR2_DL_DIR`),
  więc przebudowa kontenera nie wymusza ponownego pobierania.
- Pod WSL potrzebny jest Docker Desktop z włączoną integracją dla tej
  dystrybucji (Settings → Resources → WSL integration).
Szczegóły, mapa pamięci i rozwiązywanie problemów:
[br2-dsl2542b/README.md](br2-dsl2542b/README.md).

## Aktualizacja Buildroota

```sh
git -C buildroot fetch --tags
git -C buildroot checkout 2026.02.4
git add buildroot
git commit -m "buildroot: update to 2026.02.4"
```
