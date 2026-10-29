#!/bin/sh
#
# post-image.sh: Buildroot runs it after the images are built (the kernel with
# its initramfs is already in $BINARIES_DIR/vmlinux.bin). Here we turn it into
# a file that CFE can load over TFTP and run from RAM.
#
# Available variables (exported by Buildroot): HOST_DIR, STAGING_DIR,
# BUILD_DIR, BINARIES_DIR, BR2_CONFIG, ...
#
set -eu

BOARD_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK="$BUILD_DIR/cfe-lzma-loader"
OUT="$BINARIES_DIR/dsl2542b-ramboot.elf"

# --- Memory map (details in README.md) ---------------------------------------
# KSEG0 virtual addresses (0x80000000 = start of physical RAM, cached).
LOADADDR=0x80010000        # CFE loads the loader here and the kernel will run here
LZMA_TEXT_START=0x80a00000 # the loader relocates itself here before decompressing the kernel
CFE_MEM_START=0x80401000   # from the CFE log: "Total memory used by CFE: 0x80401000 - ..."
RAM_END=0x81000000         # 16 MB RAM
LOADER_SLACK=$((96 * 1024)) # bss + 8 KB stack + LZMA decoder buffer (~32 KB), with headroom

# --- Buildroot cross-compiler -------------------------------------------------
# STAGING_DIR = $HOST_DIR/<tuple>/sysroot, e.g. mips-buildroot-linux-uclibc
TUPLE="$(basename "$(dirname "$STAGING_DIR")")"
CROSS="$HOST_DIR/bin/$TUPLE-"
[ -x "${CROSS}gcc" ] || { echo "${CROSS}gcc not found" >&2; exit 1; }

XZ="$HOST_DIR/bin/xz"
[ -x "$XZ" ] || XZ="$(command -v xz)"

KERNEL_BIN="$BINARIES_DIR/vmlinux.bin"
KERNEL_ELF="$(ls -d "$BUILD_DIR"/linux-[0-9]*/vmlinux | head -n 1)"

rm -rf "$WORK"
mkdir -p "$WORK"

# --- 1. Kernel compression ----------------------------------------------------
# "LZMA alone" format: 13-byte header (5 B properties + 8 B size).
# xz writes the size as "unknown" (0xFF..FF), but the loader's decoder takes
# the exact output size from the header, so we patch it in by hand
# (little-endian, 64 bits).
KSIZE=$(stat -c %s "$KERNEL_BIN")
"$XZ" --format=lzma -9 -c "$KERNEL_BIN" > "$WORK/kernel.lzma"
perl -e 'print pack("V2", $ARGV[0], 0)' "$KSIZE" | \
	dd of="$WORK/kernel.lzma" bs=1 seek=5 conv=notrunc status=none

# --- 2. Building the loader ---------------------------------------------------
make -s -C "$BOARD_DIR/lzma-loader" O="$WORK" CROSS_COMPILE="$CROSS" \
	LOADADDR="$LOADADDR" LZMA_TEXT_START="$LZMA_TEXT_START" \
	LOADER_DATA="$WORK/kernel.lzma"

cp "$WORK/loader.elf" "$OUT"

# --- 3. Checking that everything fits -----------------------------------------
hex() { printf '0x%08x' "$1"; }
fail=0

# a) The ELF loaded by CFE must not overlap CFE's own memory.
LOADER_SIZE=$(stat -c %s "$WORK/loader.bin")
ELF_END=$((LOADADDR + LOADER_SIZE))
if [ "$ELF_END" -ge $((CFE_MEM_START)) ]; then
	echo "ERROR: loader.elf ends at $(hex $ELF_END), CFE starts at $(hex $CFE_MEM_START)" >&2
	fail=1
fi

# b) The decompressed kernel (including .bss) must end before the place
#    the loader relocated itself to, otherwise the loader overwrites itself.
KERNEL_END=$((0x$("${CROSS}nm" "$KERNEL_ELF" | awk '$3 == "_end" { print $1 }')))
KERNEL_END_PHYS=$((KERNEL_END & 0x9fffffff))   # in case of KSEG1 addresses
if [ "$KERNEL_END_PHYS" -ge $((LZMA_TEXT_START)) ]; then
	echo "ERROR: kernel ends at $(hex $KERNEL_END_PHYS), loader runs from $(hex $LZMA_TEXT_START)" >&2
	fail=1
fi

# c) The relocated loader + its buffers must fit in RAM.
LOADER_RUN_END=$((LZMA_TEXT_START + LOADER_SIZE + LOADER_SLACK))
if [ "$LOADER_RUN_END" -ge $((RAM_END)) ]; then
	echo "ERROR: relocated loader reaches $(hex $LOADER_RUN_END), RAM ends at $(hex $RAM_END)" >&2
	fail=1
fi

cat <<EOF

=== Image for booting from RAM via CFE ===
  file:                  $OUT
  kernel (vmlinux.bin):  $KSIZE B
  kernel.lzma:           $(stat -c %s "$WORK/kernel.lzma") B

  Memory map:
  $(hex $LOADADDR) - $(hex $ELF_END)   loader.elf loaded by CFE
  $(hex $CFE_MEM_START) - ...          CFE memory (must not be overwritten while loading)
  $(hex $LOADADDR) - $(hex $KERNEL_END_PHYS)   decompressed kernel (with .bss)
  $(hex $LZMA_TEXT_START) - $(hex $LOADER_RUN_END)   relocated loader
  $(hex $RAM_END)                       end of RAM (16 MB)

EOF

exit $fail
