# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

EE_BIN = bin/mapdownloader.elf
EE_PACKED_BIN = bin/mapdownloader.packed.elf
EE_OBJS = src/miniz.o src/main.o src/client.o src/sha1.o src/db.o src/pad.o ps2dev9_irx.o netman_irx.o smap_irx.o usbd_irx.o usbhdfsd_irx.o
EE_LIBS = -lnetman -lps2ip -ldebug -lpatches -lpad -lz
EE_INCS = -Iinclude
EE_LDFLAGS = -L$(PS2SDK)/ports/lib
EE_OPTFLAGS = -DDEBUG1 -Os -G0 -g -fdata-sections -ffunction-sections -Wl,--gc-sections -fsingle-precision-constant

all: $(EE_BIN)
	ps2-packer $(EE_BIN) $(EE_PACKED_BIN)

%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx
	
clean:
	rm -f $(EE_BIN) $(EE_OBJS)

run: $(EE_BIN)
	ps2client -h 192.168.1.243 -t 3 execee host:$(EE_BIN)

reset:
	ps2client -h 192.168.1.243 reset


include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.eeglobal
