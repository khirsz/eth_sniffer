# LZMA loader for CFE (BCM63xx) - built outside OpenWrt.
#
# Based on target/linux/brcm63xx/image/lzma-loader from OpenWrt 18.06
# (Gabor Juhos, Jonas Gorski; GPL-2.0). Changes: standalone Makefile
# building into an O= directory, no -fhonour-copts flag (it only exists in
# OpenWrt's gcc), local asm.h/regdef.h instead of kernel headers.
#
# Invocation (done by post-image.sh):
#   make -C <this directory> O=<work directory> CROSS_COMPILE=<prefix> \
#        LOADADDR=0x80010000 LZMA_TEXT_START=0x80a00000 \
#        LOADER_DATA=<kernel.lzma>
#
# Output: $(O)/loader.elf
#
# How it works:
#   1. loader.bin = loader code + compressed kernel, linked at
#      LZMA_TEXT_START (where it will RUN).
#   2. loader.elf = the same blob wrapped in an ELF that CFE LOADS at
#      LOADADDR (low in RAM, below the memory used by CFE itself).
#   3. After starting, the code copies itself to LZMA_TEXT_START (head.S),
#      decompresses the kernel to LOADADDR and jumps to it.

CROSS_COMPILE	?=
LOADADDR	?= 0x80010000
LZMA_TEXT_START	?= 0x80a00000
LOADER_DATA	?=
O		?= .

SRC		:= $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
OUT		:= $(abspath $(O))

CC		:= $(CROSS_COMPILE)gcc
LD		:= $(CROSS_COMPILE)ld
OBJCOPY		:= $(CROSS_COMPILE)objcopy
OBJDUMP		:= $(CROSS_COMPILE)objdump

ifeq ($(strip $(LOADER_DATA)),)
$(error Set LOADER_DATA=<LZMA-compressed kernel file>)
endif

BIN_FLAGS	:= -O binary -R .reginfo -R .note -R .comment -R .mdebug \
		   -R .MIPS.abiflags -S

# -G 0, -mno-abicalls, -fno-pic: code without a GOT and without gp-relative
#   data, since there is no dynamic linker or libc startup code here.
# -ffreestanding: no libc, only compiler headers (stddef, stdarg).
# -march=mips32: the BMIPS3300 core in the BCM6338 is MIPS32 rev1.
# -fno-tree-loop-distribute-patterns: newer gcc can turn a loop into a
#   memset/memcpy call, and there is no libc here to provide them.
CFLAGS		:= -D__KERNEL__ -Wall -Wstrict-prototypes -Wno-trigraphs -Os \
		   -fno-strict-aliasing -fno-common -fomit-frame-pointer -G 0 \
		   -mno-abicalls -fno-pic -ffunction-sections -pipe \
		   -ffreestanding -fno-tree-loop-distribute-patterns \
		   -mabi=32 -march=mips32 \
		   -Wa,-32 -Wa,-march=mips32 -Wa,-mips32 -Wa,--trap \
		   -D_LZMA_PROB32 -DLZMA_WRAPPER=1 -DLOADADDR=$(LOADADDR) \
		   -I$(SRC)
ASFLAGS		:= $(CFLAGS) -D__ASSEMBLY__

LDFLAGS		:= -static --gc-sections -no-warn-mismatch \
		   -e startup -T $(SRC)/loader.lds -Ttext $(LZMA_TEXT_START)

O_FORMAT	= $(shell $(OBJDUMP) -i | head -2 | grep elf32)

OBJECTS		:= $(addprefix $(OUT)/,head.o loader.o cache.o board.o \
		   printf.o LzmaDecode.o data.o)

.PHONY: all clean
all: $(OUT)/loader.elf

$(OUT):
	mkdir -p $@

$(OUT)/%.o: $(SRC)/%.c | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/%.o: $(SRC)/%.S | $(OUT)
	$(CC) $(ASFLAGS) -c -o $@ $<

# The compressed kernel as raw data in the .data.lzma section
# (the _lzma_data_start/_lzma_data_end symbols are defined by lzma-data.lds).
$(OUT)/data.o: $(LOADER_DATA) | $(OUT)
	cp $< $(OUT)/payload.lzma
	cd $(OUT) && $(LD) -r -b binary --oformat $(O_FORMAT) \
		-T $(SRC)/lzma-data.lds -o $@ payload.lzma

$(OUT)/loader: $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(OUT)/loader.bin: $(OUT)/loader
	$(OBJCOPY) $(BIN_FLAGS) $< $@

$(OUT)/loader2.o: $(OUT)/loader.bin
	cd $(OUT) && $(LD) -r -b binary --oformat $(O_FORMAT) -o $@ loader.bin

$(OUT)/loader.elf: $(OUT)/loader2.o
	$(LD) -e startup -T $(SRC)/loader2.lds -Ttext $(LOADADDR) -o $@ $<

clean:
	rm -f $(OUT)/loader $(OUT)/*.elf $(OUT)/*.bin $(OUT)/*.o $(OUT)/payload.lzma
