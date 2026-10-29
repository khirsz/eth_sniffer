# DSL-2542B + Buildroot: a port-mirroring sniffer

A custom, minimal Linux for the D-Link DSL-2542B (BCM6338 SoC, BCM5325E switch,
16 MB RAM, 4 MB NOR) that enables port mirroring by itself at boot.

Stage 1 (this one): the system boots **from RAM over TFTP**, the flash stays untouched.
Stage 2 (later): writing to flash.

---

## 1. What is in this repository

```
br2-dsl2542b/
├── external.desc            tree name: DSL2542B -> variable $(BR2_EXTERNAL_DSL2542B_PATH)
├── external.mk              includes the packages from package/*/*.mk
├── Config.in                adds our packages to menuconfig
├── configs/
│   └── dsl2542b_defconfig   the whole Buildroot configuration for this board
├── board/dlink/dsl2542b/
│   ├── linux.fragment       differences from the kernel's bcm63xx_defconfig
│   ├── post-image.sh        turns the kernel into a file for CFE and checks the memory map
│   ├── lzma-loader/         a small program that loads the kernel (from OpenWrt, GPL-2.0)
│   └── rootfs-overlay/      files overlaid onto the rootfs (IP address)
└── package/swmirror/        our package: the tool + init script
```

This is an **external tree** (*br2-external*). Buildroot stays untouched
(it is a submodule of the `eth_sniffer` repo), and everything specific to this
board lives next to it. Buildroot loads this tree via `BR2_EXTERNAL=...` and
remembers the path in `output/`, so afterwards a plain `make` in the `output/`
directory is enough (Buildroot creates a small forwarding Makefile there).

---

## 2. Buildroot in a nutshell

Buildroot is a set of Makefiles that builds a complete system from source:

```
 1. toolchain       binutils + gcc + uClibc-ng for MIPS (output/host/)
 2. packages        busybox, swmirror, ...   -> output/target/  (the future rootfs)
 3. rootfs          output/target/ -> rootfs.cpio
 4. kernel          with rootfs.cpio built in as an initramfs -> vmlinux.bin
 5. post-image.sh   vmlinux.bin -> dsl2542b-ramboot.elf      (output/images/)
```

The most important directories in `output/`:

| Directory | Contents |
|---|---|
| `build/<package>-<version>/` | unpacked and built sources of each package |
| `host/` | tools that run on your PC, including the cross-compiler `mips-buildroot-linux-uclibc-gcc` |
| `staging/` | sysroot: headers and libraries for cross-compiling |
| `target/` | what goes into the router's rootfs (no headers, stripped binaries) |
| `images/` | the final images |

Configuration:

- `configs/dsl2542b_defconfig` is a *minimal* configuration: only what
  differs from the defaults.
- `make dsl2542b_defconfig` expands it into a full `output/.config`.
- `make menuconfig` changes `.config`, and `make savedefconfig` writes the
  changes back to the file in `configs/`.
- The kernel is configured the same way, but separately: the kernel's
  `bcm63xx_defconfig` + our `linux.fragment`. Inspect it with `make linux-menuconfig`.

Buildroot **does not track dependencies between the configuration and
packages that are already built**. After changing a package you run
`make <package>-rebuild`, and after changing the architecture or toolchain
`make clean` and a full rebuild.

---

## 3. Building (WSL / Linux)

System packages (Ubuntu/Debian):

```sh
sudo apt install build-essential git rsync bc cpio unzip file wget \
                 perl python3 libncurses-dev xz-utils
```

**Build on a Linux filesystem** (e.g. `~/eth_sniffer`), not on `/mnt/c/...`.
On Windows drives the build is many times slower and can fail on
case sensitivity.

```sh
# Buildroot is a submodule of the eth_sniffer repo (pinned to tag 2026.02.3).
cd ~/eth_sniffer
git submodule update --init
# O= : build output in ~/eth_sniffer/output, outside the Buildroot submodule
make -C buildroot O=$PWD/output BR2_EXTERNAL=$PWD/br2-dsl2542b dsl2542b_defconfig
cd output
make 2>&1 | tee build.log
```

The first build takes 30 to 60 minutes, because the whole toolchain is built.
Later builds are fast. At the end post-image.sh prints the memory map:

```
=== Image for booting from RAM via CFE ===
  file:                  .../output/images/dsl2542b-ramboot.elf
  ...
```

If any of the checks fails, the script prints `ERROR:` and the build fails.
This is deliberate, because such an image would hang CFE.

Useful commands (run in the `output/` directory):

| Command | What it does |
|---|---|
| `make swmirror-rebuild all` | rebuilds our package and the images |
| `make linux-rebuild all` | rebuilds the kernel |
| `make linux-menuconfig` | kernel configuration (move the changes into `linux.fragment`) |
| `make busybox-menuconfig` | choose BusyBox applets |
| `make savedefconfig` | writes `.config` back to `configs/dsl2542b_defconfig` |

---

## 4. The memory map, or why lzma-loader exists

CFE loads an ELF file over TFTP and runs it. It has one hard limitation:
**CFE itself lives in RAM** at `0x80401000–0x80528000` (visible in the boot
log: *Total memory used by CFE*). A file whose segments fall into this area
overwrites CFE while it is being loaded.

The 6.12 kernel with its initramfs takes several MB once decompressed and must
run from `0x80010000`, so it does not fit directly. The OpenWrt solution:

```
 Step 1: CFE loads loader.elf (small: code + LZMA-compressed kernel)
   0x80010000 [loader + kernel.lzma]          0x80401000 [CFE ........]

 Step 2: the loader copies itself higher up (head.S)
   0x80010000 [old copy]      0x80a00000 [loader + kernel.lzma]

 Step 3: the loader decompresses the kernel to its proper place and jumps to it
   0x80010000 [kernel ............]           0x80a00000 [loader]
```

After step 3 neither CFE nor the loader is needed, so the kernel uses all of RAM.
`post-image.sh` checks all three conditions:

1. loader.elf ends before `0x80401000`,
2. the decompressed kernel (including `.bss`, i.e. the `_end` symbol) ends before `0x80a00000`,
3. the relocated loader fits in 16 MB.

Compression: `xz --format=lzma` writes a header with the size set to "unknown",
but the loader's decoder needs the exact output size, so the script patches it
into the header (bytes 5–12, little-endian).

---

## 5. Booting from RAM via CFE (no flashing)

### TFTP server

The easiest option is to run it on a Raspberry Pi on the router's LAN:

```sh
sudo apt install tftpd-hpa             # directory: /srv/tftp
scp output/images/dsl2542b-ramboot.elf pi@192.168.1.5:/tmp/
ssh pi@192.168.1.5 sudo cp /tmp/dsl2542b-ramboot.elf /srv/tftp/
```

A server in WSL2 sits behind Windows NAT by default and the router cannot reach
it. If you want to serve from the laptop, use a Windows program
(e.g. Tftpd64) or enable "mirrored" networking mode in WSL.

### CFE settings

On the UART console, interrupt autoboot with any key and type `c`:

```
Board IP address                  : 192.168.1.1:ffffff00
Host IP address                   : 192.168.1.5          <- TFTP server
Run from flash/host (f/h)         : h
Default host run file name        : dsl2542b-ramboot.elf
```

Leave the remaining fields unchanged (Enter), including
"Default host flash file name", which is only used by the `f` command
(flashing). Then `r`.

The `h` flag is saved in NVRAM, so after every power-on the router will boot
your image from TFTP. While working on the system this is convenient: after a
rebuild you just replace the file and restart the router. To go back to the
original firmware, set `f` again.

### What to expect on the UART

```
OpenWrt kernel loader for BCM63XX
...
Decompressing kernel... done!
Starting kernel at 80010000...

Linux version 6.12.91 ...
board_bcm963xx: CFE version: 1.0.37-8.7
...
eth0: link forced UP - 100/full - flow control off/off
...
Starting port mirroring (lan1 -> lan4, both): OK
...
DSL-2542B / Buildroot
dsl2542b login: root
# swmirror status
```

Login: `root`, no password.

---

## 6. swmirror

```sh
swmirror status                  # port link and mirroring state
swmirror on lan4 lan1            # traffic from LAN1 (both directions) -> LAN4
swmirror on lan4 lan1,lan2 rx    # only traffic received on LAN1 and LAN2
swmirror on lan4 cpu             # all traffic of the router CPU
swmirror off
swmirror read 0x02 0x10          # raw register: page, address
swmirror write 0x02 0x10 0x8001
```

At boot, mirroring is enabled by `/etc/init.d/S41swmirror` according to
`/etc/swmirror.conf` (LAN1 -> LAN4 by default). The default file is in
`package/swmirror/swmirror.conf`.

The tool does exactly what `setmem` did before, but through the driver:
the `SIOCGMIIREG`/`SIOCSMIIREG` ioctls on `eth0` with PHY address 30.
The `bcm63xx_enet` driver has no PHY attached on the 96338W board, so it
passes any PHY address straight to the MDIO bus. Unlike the stock firmware,
nothing polls MDIO in the background, so there is no race for the register.

Port mapping: LAN1 = port 3 and LAN4 = port 0 verified on hardware,
LAN2 = 2 and LAN3 = 1 taken from the OpenWrt configuration for 96338W.

---

## 7. When something does not work

| Symptom | What to check |
|---|---|
| CFE does not download the file | Host IP and file name in `c`, whether the TFTP server responds (`tftp` from another machine), whether the cable is in a LAN port |
| CFE downloads the file but rejects it | Note the exact message. This CFE version might expect a format other than ELF |
| Silence after `Starting kernel` | The kernel is starting but has no console. Check `CONFIG_CMDLINE` and `CONFIG_SERIAL_BCM63XX_CONSOLE` in `make linux-menuconfig` |
| `unknown bcm963xx board` | The kernel did not recognise the Board ID from NVRAM, which should be `96338W` (`p` command in CFE) |
| `nvram checksum failed` | Harmless on this board: the old CFE uses a different NVRAM layout than mainline expects, but the board name is still read correctly |
| No network after boot | The switch is configured by CFE, the kernel does not touch it. Check `swmirror status` and `ip addr` |
| `swmirror`: `Operation not supported` | The ioctl did not reach bcm63xx_enet. Check that `eth0` exists (`ip link`) |

---

## 8. What next

- **Flash:** an image with an imagetag header (Board ID `6338W`), written with
  `f` in CFE or through web recovery. A backup of the original flash is already done.
- **Network access:** BusyBox `telnetd` or `dropbear` (SSH).
- **LED exercise:** `ls /sys/class/leds/` and
  `echo heartbeat > /sys/class/leds/<led>/trigger`.
