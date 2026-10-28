# Compila sdt_loader.nro con devkitA64 + libnx (sin plantillas externas)
DEVKITPRO ?= /opt/devkitpro
export PATH := $(DEVKITPRO)/devkitA64/bin:$(DEVKITPRO)/tools/bin:$(PATH)

TARGET := sdt_loader
OBJS   := main.o elfload.o nxplat.o dl.o jni.o shims.o stubs.o
HDRS   := elfload.h sdt.h stubs.inc jni_stubs.inc jni_names.inc
LIBNX  := $(DEVKITPRO)/libnx
ARCH   := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CC     := aarch64-none-elf-gcc
CFLAGS := -g -O2 -Wall -ffunction-sections -D__SWITCH__ $(ARCH) \
          -I$(LIBNX)/include -I$(DEVKITPRO)/portlibs/switch/include
LDFLAGS:= -specs=$(LIBNX)/switch.specs -g $(ARCH) -L$(LIBNX)/lib
LIBS   := -lnx -lm

all: $(TARGET).nro

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(TARGET).nacp:
	nacptool --create "SDT Loader" "SDT-NX" "0.3b" $@

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp icon.jpg
	elf2nro $(TARGET).elf $@ --nacp=$(TARGET).nacp --icon=icon.jpg

clean:
	rm -f *.o *.elf *.nro *.nacp
