# edtest - the EverDrive PRO 16 MB RAM test: a 32X cartridge ROM with "SEGA SSF" (CD32X BootCamp)
#   make ROOTDIR=/opt/toolchains/sega ED_CTRL0=0xA000   -> edtest.32x   (edtest/build.sh makes both ROMs)
ROOTDIR ?= /opt/toolchains/sega
SHROOT  ?= $(ROOTDIR)
MDROOT  ?= $(ROOTDIR)

TARGET  = edsave
BUILD   = build

SHPREFIX = $(SHROOT)/sh-elf/bin/sh-elf-
SHCC     = $(SHPREFIX)gcc
SHAS     = $(SHPREFIX)as
SHOBJC   = $(SHPREFIX)objcopy
CC_VER  := $(shell $(SHCC) -dumpversion)

LIBPATH  = -L$(SHROOT)/sh-elf/lib -L$(SHROOT)/sh-elf/lib/gcc/sh-elf/$(CC_VER) -L$(SHROOT)/sh-elf/sh-elf/lib
INCPATH  = -Isrc -I$(SHROOT)/sh-elf/include -I$(SHROOT)/sh-elf/sh-elf/include
CCFLAGS  = -D__32X__ -m2 -mb -O2 -Wall -c -fomit-frame-pointer -std=gnu99
# ED_CTRL0: the value written to the EverDrive's mapper register CTRL0 (0xA130F0)
#   0xA000 = P + W, as the Pro write-ups show it (run this one first)
#   0xE000 = P + X (32X mode) + W (write), as the EverDrive X-series notes give it
ED_CTRL0 ?= 0xA000
CCFLAGS += -DED_CTRL0=$(ED_CTRL0)
LDFLAGS  = -T $(SHROOT)/ldscripts/mars.ld -Wl,-Map=$(BUILD)/output.map -nostdlib
ED_SRAM ?= 1
ASFLAGS  = --big $(if $(filter 1,$(ED_SRAM)),--defsym ED_SRAM=1,)
LIBS     = $(LIBPATH) -lc -lgcc -lgcc-Os-4-200 -lnosys

# crt0.o must be first: it holds the ROM header, the 32X header and the SH2 start-up
OBJS = $(BUILD)/crt0.o $(BUILD)/main.o

all: $(TARGET).32x

$(TARGET).32x: $(TARGET).elf
	$(SHOBJC) -O binary $< $(BUILD)/temp.bin
	dd if=$(BUILD)/temp.bin of=$@ bs=128K conv=sync status=none

$(TARGET).elf: src-md/m68k.bin $(OBJS)
	$(SHCC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

# the 68000 side of the cartridge (never run on the CD32X: the 68000 runs the CD listener there)
src-md/m68k.bin:
	$(MAKE) -C src-md ROOTDIR=$(MDROOT)

$(BUILD)/crt0.o: src/crt0.s src-md/m68k.bin | $(BUILD)
	$(SHAS) $(ASFLAGS) -I. -o $@ $<

$(BUILD)/%.o: src/%.c src/32x.h | $(BUILD)
	$(SHCC) $(CCFLAGS) $(INCPATH) -o $@ $<

$(BUILD):
	mkdir -p $@

clean:
	$(MAKE) -C src-md clean ROOTDIR=$(MDROOT)
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).32x
