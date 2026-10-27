#!/bin/sh
#
# post-image.sh: Buildroot uruchamia go po zbudowaniu obrazów (kernel z
# initramfs leży już w $BINARIES_DIR/vmlinux.bin). Tu robimy z niego plik,
# który CFE potrafi załadować przez TFTP i uruchomić z RAM-u.
#
# Dostępne zmienne (eksportuje je Buildroot): HOST_DIR, STAGING_DIR,
# BUILD_DIR, BINARIES_DIR, BR2_CONFIG, ...
#
set -eu

BOARD_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK="$BUILD_DIR/cfe-lzma-loader"
OUT="$BINARIES_DIR/dsl2542b-ramboot.elf"

# --- Mapa pamięci (szczegóły w README.md) -----------------------------------
# Adresy wirtualne KSEG0 (0x80000000 = początek fizycznego RAM-u, z cache).
LOADADDR=0x80010000        # tu CFE ładuje loader i tu kernel będzie działał
LZMA_TEXT_START=0x80a00000 # tu loader przenosi się przed rozpakowaniem kernela
CFE_MEM_START=0x80401000   # z logu CFE: "Total memory used by CFE: 0x80401000 - ..."
RAM_END=0x81000000         # 16 MB RAM
LOADER_SLACK=$((96 * 1024)) # bss + stos 8 KB + bufor dekodera LZMA (~32 KB) z zapasem

# --- Cross-kompilator Buildroota ---------------------------------------------
# STAGING_DIR = $HOST_DIR/<krotka>/sysroot, np. mips-buildroot-linux-uclibc
TUPLE="$(basename "$(dirname "$STAGING_DIR")")"
CROSS="$HOST_DIR/bin/$TUPLE-"
[ -x "${CROSS}gcc" ] || { echo "Nie znaleziono ${CROSS}gcc" >&2; exit 1; }

XZ="$HOST_DIR/bin/xz"
[ -x "$XZ" ] || XZ="$(command -v xz)"

KERNEL_BIN="$BINARIES_DIR/vmlinux.bin"
KERNEL_ELF="$(ls -d "$BUILD_DIR"/linux-[0-9]*/vmlinux | head -n 1)"

rm -rf "$WORK"
mkdir -p "$WORK"

# --- 1. Kompresja kernela ----------------------------------------------------
# Format "LZMA alone": 13-bajtowy nagłówek (5 B właściwości + 8 B rozmiar).
# xz zapisuje rozmiar jako "nieznany" (0xFF..FF), a dekoder w loaderze bierze
# z nagłówka dokładny rozmiar wyjścia, więc wpisujemy go ręcznie
# (little-endian, 64 bity).
KSIZE=$(stat -c %s "$KERNEL_BIN")
"$XZ" --format=lzma -9 -c "$KERNEL_BIN" > "$WORK/kernel.lzma"
perl -e 'print pack("V2", $ARGV[0], 0)' "$KSIZE" | \
	dd of="$WORK/kernel.lzma" bs=1 seek=5 conv=notrunc status=none

# --- 2. Budowanie loadera ----------------------------------------------------
make -s -C "$BOARD_DIR/lzma-loader" O="$WORK" CROSS_COMPILE="$CROSS" \
	LOADADDR="$LOADADDR" LZMA_TEXT_START="$LZMA_TEXT_START" \
	LOADER_DATA="$WORK/kernel.lzma"

cp "$WORK/loader.elf" "$OUT"

# --- 3. Sprawdzenie, czy wszystko się mieści ---------------------------------
hex() { printf '0x%08x' "$1"; }
fail=0

# a) ELF ładowany przez CFE nie może nachodzić na pamięć samego CFE.
LOADER_SIZE=$(stat -c %s "$WORK/loader.bin")
ELF_END=$((LOADADDR + LOADER_SIZE))
if [ "$ELF_END" -ge $((CFE_MEM_START)) ]; then
	echo "BŁĄD: loader.elf kończy się na $(hex $ELF_END), CFE zaczyna się od $(hex $CFE_MEM_START)" >&2
	fail=1
fi

# b) Rozpakowany kernel (razem z .bss) musi się skończyć przed miejscem,
#    do którego przeniósł się loader, bo inaczej nadpisze sam siebie.
KERNEL_END=$((0x$("${CROSS}nm" "$KERNEL_ELF" | awk '$3 == "_end" { print $1 }')))
KERNEL_END_PHYS=$((KERNEL_END & 0x9fffffff))   # na wypadek adresów KSEG1
if [ "$KERNEL_END_PHYS" -ge $((LZMA_TEXT_START)) ]; then
	echo "BŁĄD: kernel kończy się na $(hex $KERNEL_END_PHYS), loader działa od $(hex $LZMA_TEXT_START)" >&2
	fail=1
fi

# c) Przeniesiony loader + jego bufory muszą się zmieścić w RAM.
LOADER_RUN_END=$((LZMA_TEXT_START + LOADER_SIZE + LOADER_SLACK))
if [ "$LOADER_RUN_END" -ge $((RAM_END)) ]; then
	echo "BŁĄD: loader po relokacji sięga $(hex $LOADER_RUN_END), RAM kończy się na $(hex $RAM_END)" >&2
	fail=1
fi

cat <<EOF

=== Obraz do uruchomienia z RAM przez CFE ===
  plik:                  $OUT
  kernel (vmlinux.bin):  $KSIZE B
  kernel.lzma:           $(stat -c %s "$WORK/kernel.lzma") B

  Mapa pamięci:
  $(hex $LOADADDR) - $(hex $ELF_END)   loader.elf ładowany przez CFE
  $(hex $CFE_MEM_START) - ...          pamięć CFE (nie wolno jej nadpisać przy ładowaniu)
  $(hex $LOADADDR) - $(hex $KERNEL_END_PHYS)   rozpakowany kernel (z .bss)
  $(hex $LZMA_TEXT_START) - $(hex $LOADER_RUN_END)   loader po relokacji
  $(hex $RAM_END)                       koniec RAM (16 MB)

EOF

exit $fail
