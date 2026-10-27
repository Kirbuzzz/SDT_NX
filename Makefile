# Compila sdt_loader.nro con devkitA64 + libnx (sin plantillas externas)
DEVKITPRO ?= /opt/devkitpro
export PATH := $(DEVKITPRO)/devkitA64/bin:$(DEVKITPRO)/tools/bin:$(PATH)

TARGET := sdt_loader
OBJS   := main.o elfload.o
LIBNX  := $(DEVKITPRO)/libnx
ARCH   := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CC     := aarch64-none-elf-gcc
CFLAGS := -g -O2 -Wall -ffunction-sections -D__SWITCH__ $(ARCH) \
          -I$(LIBNX)/include -I$(DEVKITPRO)/portlibs/switch/include
LDFLAGS:= -specs=$(LIBNX)/switch.specs -g $(ARCH) -L$(LIBNX)/lib
LIBS   := -lnx

all: $(TARGET).nro

%.o: %.c elfload.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(TARGET).nacp:
	nacptool --create "SDT Loader" "SDT-NX" "0.2" $@

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp icon.jpg
	elf2nro $(TARGET).elf $@ --nacp=$(TARGET).nacp --icon=icon.jpg

clean:
	rm -f *.o *.elf *.nro *.nacp
