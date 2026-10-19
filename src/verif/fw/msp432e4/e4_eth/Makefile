# =============================================================================
# Makefile — e4_eth, eco TCP/UDP con lwIP 2.2.1 sobre el EMAC del MSP432E401Y (T7)
#
# DriverLib del E4 (../sdk, BSD-3) y el nucleo de lwIP (lwip/, BSD-3; de donde
# sale, en LEEME.md), compilados de sus fuentes. Mismo compilador;
# -ffile-prefix-map por la huella (T-22).
#
#   make            # genera e4_eth.out y e4_eth.bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
DL     := ../sdk/ti/devices/msp432e4/driverlib
LW     := lwip/src

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O2 -g -ffreestanding -fno-builtin -std=gnu11 -Wall \
          -D__MSP432E401Y__ -I. -I../sdk -I../../cmsis/Include -I$(LW)/include \
          -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T ../msp432e401y.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=e4_eth.map -Wl,--no-warn-rwx-segments

LWIP_C := $(LW)/core/init.c $(LW)/core/def.c $(LW)/core/inet_chksum.c $(LW)/core/ip.c \
          $(LW)/core/mem.c $(LW)/core/memp.c $(LW)/core/netif.c $(LW)/core/pbuf.c \
          $(LW)/core/tcp.c $(LW)/core/tcp_in.c $(LW)/core/tcp_out.c $(LW)/core/timeouts.c \
          $(LW)/core/udp.c $(LW)/core/ipv4/etharp.c $(LW)/core/ipv4/icmp.c \
          $(LW)/core/ipv4/ip4.c $(LW)/core/ipv4/ip4_addr.c $(LW)/core/ipv4/ip4_frag.c \
          $(LW)/netif/ethernet.c
DL_O   := sysctl.o emac.o interrupt.o cpu.o systick.o
OBJS   := startup.o main.o emac_lwip.o libc_min.o $(DL_O) $(notdir $(LWIP_C:.c=.o))

vpath %.c $(DL) $(LW)/core $(LW)/core/ipv4 $(LW)/netif

all: e4_eth.out e4_eth.bin

startup.o: ../startup_e4.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o emac_lwip.o: emac_lwip.h lwipopts.h arch/cc.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(DL_O): %.o: $(DL)/%.c
	$(CC) $(CFLAGS) -g0 -Wno-unused-parameter -c -o $@ $<

e4_eth.out: $(OBJS) ../msp432e401y.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

e4_eth.bin: e4_eth.out
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
