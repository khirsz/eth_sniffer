# eth_sniffer: D-Link DSL-2542B as a hardware Ethernet sniffer

An old ADSL router (BCM6338 SoC, BCM5325E switch) running its own Linux
built with Buildroot. At boot the system enables port mirroring in the switch:
traffic from a chosen LAN port is copied to the port with the laptop running
Wireshark.

## Contents

```
eth_sniffer/
├── buildroot/       submodule: Buildroot 2026.02.3 (unmodified)
├── br2-dsl2542b/    br2-external tree: defconfig, kernel, CFE loader, swmirror
└── dump_flash.py    flash dump over the UART (dumpmem in the stock firmware)
```

## Building

```sh
git clone --recurse-submodules https://github.com/khirsz/eth_sniffer.git
cd eth_sniffer
make -C buildroot O=$PWD/output BR2_EXTERNAL=$PWD/br2-dsl2542b dsl2542b_defconfig
cd output
make 2>&1 | tee build.log
```

Output: `output/images/dsl2542b-ramboot.elf` (boot from RAM over TFTP from CFE).

### In a container (Dev Containers)

`.devcontainer/` describes a Debian 13 image with the packages Buildroot
requires from the host. Buildroot builds the cross-toolchain itself anyway.
VS Code: *Dev Containers: Reopen in Container*, then run the same commands
as above in the container terminal.

- The repo is mounted at **the same path** as on the host, because Buildroot
  writes absolute paths into `output/`. This way `output/` works on both
  sides.
- Downloaded sources go to the `dsl2542b-br-dl` volume (`BR2_DL_DIR`),
  so rebuilding the container does not force a new download.
- Under WSL you need Docker Desktop with integration enabled for this
  distribution (Settings → Resources → WSL integration).

Details, the memory map and troubleshooting:
[br2-dsl2542b/README.md](br2-dsl2542b/README.md).

## Updating Buildroot

```sh
git -C buildroot fetch --tags
git -C buildroot checkout 2026.02.4
git add buildroot
git commit -m "buildroot: update to 2026.02.4"
```
