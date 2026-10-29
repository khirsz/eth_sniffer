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
Configuring switch port isolation: OK
Starting port mirroring (lan4 -> lan2, both): OK
...
eth0: link forced UP - 100/full - flow control off/off
...
Starting udhcpd: OK
Starting dropbear sshd: OK
...
DSL-2542B / Buildroot
dsl2542b login: root
```

Login: `root`, password `sniffer` (`BR2_TARGET_GENERIC_ROOT_PASSWD` in the
defconfig), both on the UART and over SSH. After login a banner shows the
port roles, the current switch state and how to change it
(`/etc/profile.d/sniffer.sh`).

### SSH from the laptop

Plug the laptop into **LAN1**. It gets an address from 192.168.15.100-150
(BusyBox `udhcpd`), without a default route or DNS, so its internet access
over Wi-Fi keeps working. Then:

```sh
ssh root@192.168.15.1
```

The root filesystem is an initramfs in RAM, so dropbear generates a new host
key at every boot and ssh will complain about a changed key. For this
lab device it is fine to skip the check, e.g. in `~/.ssh/config`:

```
Host dsl2542b
    HostName 192.168.15.1
    User root
    UserKnownHostsFile /dev/null
    StrictHostKeyChecking no
```

Note: CFE keeps its own address (192.168.1.1 for TFTP) independent of Linux.
Once Linux runs, only LAN1 reaches the CPU, so the TFTP server (Pi) and the
laptop must share LAN1 (through a small external switch) if both are needed.

---

## 6. Port layout and swmirror

### Default layout: a passive tap

| Case | Switch port | Role |
|---|---|---|
| LAN1 | 3 | configuration: SSH + DHCP, 192.168.15.1, talks only to the CPU |
| LAN2 | 2 | capture: Wireshark laptop, copy of LAN4 traffic (rx+tx) |
| LAN3 | 1 | far side of the tapped link |
| LAN4 | 0 | device under observation |
| CPU (MII) | 8 | router Linux, talks only to LAN1 |

LAN4 and LAN3 are switched in hardware as if joined by a cable; the switch
also copies every frame LAN4 receives and sends to LAN2. The CPU never
touches that traffic, so the tap runs at full wire speed.

Two switch features make this work:

**Port mirroring** (page 0x02, see the comment at the top of `swmirror.c`):
ingress `0x12 = 0x0001` and egress `0x1C = 0x0001` select LAN4 (bit 0),
capture `0x10 = 0x8004` = `MIRROR_ENABLE` (bit 15) | LAN2 (bit 2).
`CAPTURE_PORT` has a bitmask format, but the datasheet (Table 64) says it
identifies a *single unique port*: **only one capture port is possible.**
LAN4 at 100 Mbit/s full duplex can produce 2x100 Mbit/s, while LAN2 can
send only 100 Mbit/s, so under heavy load some copies are dropped.

**Port-based VLAN** (page 0x31, Tables 137/138): one 16-bit register per
INGRESS port, `[8:0] FORWARD_MASK` = the egress ports this port may send to
(bit 8 = MII/CPU, bits 4:0 = ports), `[15:9]` reserved. Register addresses:
port N at `N*2`, but the MII port at **`0x10`**. The switch ANDs the ARL
forwarding decision with this mask, so it filters broadcast and flooding too.

| Reg | Ingress port | Mask | Meaning |
|---|---|---|---|
| 0x00 | LAN4 (0) | `0x0002` | only -> LAN3 |
| 0x02 | LAN3 (1) | `0x0001` | only -> LAN4 |
| 0x04 | LAN2 (2) | `0x0000` | the capture laptop cannot send anywhere |
| 0x06 | LAN1 (3) | `0x0100` | only -> CPU |
| 0x10 | CPU (8)  | `0x0008` | only -> LAN1, so DHCP/SSH stay on LAN1 |

Port 4 has no socket on this board and keeps its reset value.

### swmirror

```sh
swmirror status                  # link, mirroring and PVLAN state
swmirror on lan2 lan4            # traffic of LAN4 (both directions) -> LAN2
swmirror on lan2 lan4,lan3 rx    # only traffic received on LAN4 and LAN3
swmirror off
swmirror pvlan                   # show the PVLAN masks
swmirror pvlan lan3 lan4         # LAN3 may forward only to LAN4
swmirror pvlan lan2 none         # LAN2 may not forward anywhere
swmirror pvlan lan2 all          # reset value 0x1ff
swmirror read 0x02 0x10          # raw register: page, address
swmirror write 0x02 0x10 0x8004
```

`pvlan` does a read-modify-write (keeps bits 15:9) and reads the register
back, the same way `on` checks its writes.

At boot `/etc/init.d/S38swmirror` applies `/etc/swmirror.conf`: first the
`PVLAN_*` masks, then mirroring. It runs **before** `S40network` on purpose:
the isolation has to be in place before eth0 comes up, otherwise the router's
ARP/DHCP would briefly reach the tapped segment. The MII ioctls work while
eth0 is down (the kernel only checks that the device is present).
`S38swmirror stop` disables mirroring and restores every mask to `all`.
The default file is in `package/swmirror/swmirror.conf`; changes made on the
router are lost at reboot (initramfs).

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
| No DHCP / SSH on LAN1 | `swmirror pvlan` on the UART: LAN1 must be `cpu`, CPU must be `lan1`. `ps` should show `udhcpd` and `dropbear` |
| LAN4 and LAN3 do not see each other | `swmirror pvlan`: LAN4 -> `lan3`, LAN3 -> `lan4`. Link of both ports in `swmirror status` |
| Nothing arrives on LAN2 | Mirroring state in `swmirror status`. If it is enabled but LAN2 stays silent, the PVLAN masks may also filter mirrored copies on this chip: try `swmirror pvlan lan4 lan3,lan2` and `swmirror pvlan lan3 lan4,lan2` |

---

## 8. What next

- **Flash:** an image with an imagetag header (Board ID `6338W`), written with
  `f` in CFE or through web recovery. A backup of the original flash is already done.
- **Active mode:** put the CPU into the LAN4 <-> LAN3 path (802.1Q per-port
  VLANs on page 0x34 + a Linux bridge), so frames can be inspected or changed
  in software. Possible on this hardware, but limited by the 240 MHz CPU to a
  few Mbit/s.
- **LED exercise:** `ls /sys/class/leds/` and
  `echo heartbeat > /sys/class/leds/<led>/trigger`.
