# LZMA loader for CFE

The code comes from OpenWrt 18.06 (`target/linux/brcm63xx/image/lzma-loader/src`,
commit `9f2a40c72fdb`), authors: Gabor Juhos, Jonas Gorski and others, license
GPL-2.0. Changes in this copy:

- `head.S` includes local `asm.h` and `regdef.h` instead of kernel headers,
- new `Makefile` (builds into an `O=` directory, no `-fhonour-copts`),
- all other files unchanged.

Why this loader exists is explained in the main `README.md` (memory map section).
