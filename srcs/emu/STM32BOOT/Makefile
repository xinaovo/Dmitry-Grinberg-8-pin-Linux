#
#	(c) 2021 Dmitry Grinberg   https://dmitry.gr
#	Non-commercial use only OR licensing@dmitry.gr
#

LDFLAGS		= -lm -g
CCFLAGS		= -fno-math-errno -Os

ZWT_ADDR = 0x20001FF8
CCFLAGS	+= -Os -Wall -Wextra -Werror -mthumb -ffast-math -march=armv6-m -mcpu=cortex-m0plus -I. -mfloat-abi=soft -mno-unaligned-access -flto
CCFLAGS	+= -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-unused-function -Wno-unused-parameter -nostdlib
CCFLAGS	+= -Wno-unused-but-set-variable -Wno-unused-variable -fno-jump-tables -ffixed-r11
CCFLAGS	+= -DZWT_ADDR=$(ZWT_ADDR) -DTICKS_PER_SECOND=16000000 -DSUPPORT_DEBUG_PRINTF -DMINIMIZE_SIZE -DCPU_STM32G0 -I.. -DUSART_NEEDS_SECONDARY_INIT
LDFLAGS += -Wl,--gc-sections -Wl,-T $(LKR)  $(CCFLAGS)
CC		= arm-none-eabi-gcc
SOURCES += crt_mini.c printf.c ufat.c sd.c sdHwStm32G0sdio.c timebase.c bbUartTx.c main.c
LKR		= linker_stm32g0.lkr


LDFLAGS += $(CCFLAGS)	#for lto

APP		= bootload

#no changes below please

OBJS	= $(patsubst %.c,%.o,$(patsubst %.S,%.o,$(SOURCES)))

all:	$(APP).bin

$(APP).elf: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o : %.c Makefile
	$(CC) $(CCFLAGS) -c $< -o $@

%.o : %.S Makefile
	$(CC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(APP) $(OBJS)

%.bin: %.elf
	arm-none-eabi-objcopy -O binary $< $@ -j.text -j.rodata -j.data -j.vectors

test: $(APP).bin
	sudo CortexProg power on write $< trace $(ZWT_ADDR) info


