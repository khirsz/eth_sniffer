# LZMA loader dla CFE (BCM63xx) - budowanie poza OpenWrt.
#
# Na podstawie target/linux/brcm63xx/image/lzma-loader z OpenWrt 18.06
# (Gabor Juhos, Jonas Gorski; GPL-2.0). Zmiany: samodzielny Makefile
# z budowaniem do katalogu O=, bez flagi -fhonour-copts (jest tylko w gcc
# z OpenWrt), lokalne asm.h/regdef.h zamiast nagłówków kernela.
#
# Wywołanie (robi to post-image.sh):
#   make -C <ten katalog> O=<katalog roboczy> CROSS_COMPILE=<prefiks> \
#        LOADADDR=0x80010000 LZMA_TEXT_START=0x80a00000 \
#        LOADER_DATA=<kernel.lzma>
#
# Wynik: $(O)/loader.elf
#
# Jak to działa:
#   1. loader.bin = kod loadera + skompresowany kernel, zlinkowany pod
#      LZMA_TEXT_START (tam będzie DZIAŁAŁ).
#   2. loader.elf = ten sam blob opakowany w ELF, który CFE ŁADUJE pod
#      LOADADDR (nisko w RAM, poniżej pamięci zajętej przez samo CFE).
#   3. Po starcie kod sam się kopiuje pod LZMA_TEXT_START (head.S),
#      rozpakowuje kernel pod LOADADDR i skacze do niego.

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
$(error Podaj LOADER_DATA=<plik z kernelem skompresowanym LZMA>)
endif

BIN_FLAGS	:= -O binary -R .reginfo -R .note -R .comment -R .mdebug \
		   -R .MIPS.abiflags -S

# -G 0, -mno-abicalls, -fno-pic: kod bez GOT i bez gp-relative danych,
#   bo nie ma tu dynamicznego linkera ani startowego kodu libc.
# -ffreestanding: brak libc, tylko nagłówki kompilatora (stddef, stdarg).
# -march=mips32: rdzeń BMIPS3300 w BCM6338 to MIPS32 rev1.
# -fno-tree-loop-distribute-patterns: nowsze gcc potrafią zamienić pętlę
#   na wywołanie memset/memcpy, a tu nie ma libc, która by je dostarczyła.
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

# Skompresowany kernel jako surowe dane w sekcji .data.lzma
# (symbole _lzma_data_start/_lzma_data_end definiuje lzma-data.lds).
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
