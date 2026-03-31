#
#	(c) 2021 Dmitry Grinberg   https://dmitry.gr
#	Non-commercial use only OR licensing@dmitry.gr
#

SOURCES		= mem.c decBus.c dz11.c
LDFLAGS		= -lm -g
CCFLAGS		= -g -ggdb3 -fno-math-errno -Wno-missing-attributes	# -flto	#LTO does make things smaller
CPU			?= stm32g0
FPU			?= minimal
ULTRIX		?= no						#ultrix support?

ifeq ($(ULTRIX),yes)
	CCFLAGS += -DULTRIX_SUPPORT
	SOURCES += esar.c lance.c sii.c scsiDevice.c scsiDisk.c scsiNothing.c
endif

ifeq ($(CPU),stm32g0)
	ZWT_ADDR = 0x20001FF8
	CCFLAGS	+= -Os -Wall -Wextra -Werror -mthumb -fsingle-precision-constant -ffast-math -march=armv6-m -mcpu=cortex-m0plus -I. -mfloat-abi=soft -flto
	CCFLAGS	+= -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-unused-function -Wno-unused-parameter
	CCFLAGS	+= -Wno-unused-but-set-variable -Wno-unused-variable -ffixed-r11
	CCFLAGS	+= -D"err_str(...)=pr(__VA_ARGS__)"
	CCFLAGS	+= -DSLOW_FLASH -DCPU_STM32G0 -DCPU_TYPE_CM0 -DZWT_ADDR=$(ZWT_ADDR) -DVARIABLE_TICK_RATE
	CCFLAGS	+= -DICACHE_NUM_SETS_ORDER=5 -DOPTIMAL_RAM_WR_SZ=32 -DOPTIMAL_RAM_RD_SZ=32 -DNO_FRAMEBUFFER -DUART_RX_ONLY
	CCFLAGS	+= -DSUPPORT_DEBUG_PRINTF -DNO_RAM_LIMITITING_IN_CORE -DUSART_NEEDS_SECONDARY_INIT
	LDFLAGS += -Wl,--gc-sections -Wl,-T $(LKR) $(CCFLAGS)
	CC		= arm-none-eabi-gcc
	SOURCES += crt_stm32g0.c printf.c sd.c ucHwStm32G0.c timebase.c main8PL.c spiRamStm32G0.c bbUartTx.c usartStm.c ds1287stm32.c
	SOURCES += cpuAsm.S
	SOURCES += sdHwStm32G0sdio.c
	LKR		= linker_stm32g0.lkr

	WRITE_ADDR = 0x08002000
endif

ifeq ($(FPU),full)
	SOURCES += fpu.c
	CCFLAGS += -DFPU_SUPPORT_FULL
else ifeq ($(FPU),minimal)
	SOURCES += fpu.c
	CCFLAGS += -DFPU_SUPPORT_MINIMAL
else ifeq ($(FPU),medium)
	SOURCES += fpu.c
	CCFLAGS += -DFPU_SUPPORT_MEDIUM
else
	CCFLAGS += -DFPU_SUPPORT_NONE
endif

LDFLAGS += $(CCFLAGS)

APP		= uMIPS

#no changes below please

all:	$(APP).bin

OBJS	= $(patsubst %.c,%.o,$(patsubst %.S,%.o,$(SOURCES)))

$(APP): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o : %.c Makefile
	$(CC) $(CCFLAGS) -c $< -o $@

%.o : %.S Makefile
	$(CC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(APP).bin $(APP) $(OBJS)

%.bin: %
	arm-none-eabi-objcopy -O binary $< $@ -j.text -j.rodata -j.data -j.vectors

