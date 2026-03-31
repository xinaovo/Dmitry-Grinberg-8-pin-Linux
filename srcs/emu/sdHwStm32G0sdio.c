/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stm32g031xx.h>
#include <string.h>
#include "timebase.h"
#include "printf.h"
#include "sdHw.h"




/*
	current prototyping platfom has is as:

		PIN		func		use
		1		PB7			USART1_RX SPI2_MOSI
				PB8			SPI2_SCK
				PB9	
				PC14
		4		PA0			SPI2_SCK
				PA1			SPI1_SCK
				PA2			SPI1_MOSI USART2_TX
				PF2
		5		PA8
				PA9			USART1_TX SPI2_MISO
				PA11		SPI1_MISO
				PB0			
				PB1			
		6		PA10		SPI2_MOSI USART1_RX
				PA12		SPI1_MOSI
		7		PA13		SWDIO
		8		PB5			SPI1_MOSI
				PB6			USART1_TX SPI2_MISO
				PA14		SWCLK USART2_TX
				PA15		USART2_RX

		for current test: 1 = (PB8)ram_nCS, 4 = (PA1)SPI1_SCK, 5 = (PA11)SPI1_MISO, 6 = (PA12)SPI1_MOSI

		//RAM_nCS = SD_CLK(PB8) 
		//RAM_CLK = SD_CMD(PA1) 
		//RAM_MOSI = SD_DAT(PA12)

*/


#define PIN_CMD_PORT		GPIOA
#define PIN_CMD_PIN			1

#define PIN_CLK_PORT		GPIOB
#define PIN_CLK_PIN			8

#define PIN_DAT_PORT		GPIOA
#define PIN_DAT_PIN			12

#define PIN_DBG_PORT		GPIOA
#define PIN_DBG_PIN			11

//#define SUPPORT_LOGIC_ANALYSIS

static uint16_t mTimeoutBytes;
static uint32_t mRdTimeoutTicks, mWrTimeoutTicks, mRdTimeoutClocks, mWrTimeoutClocks;
static uint16_t mRCA;
static uint8_t mSdDelayTicks;

static bool (*mSdRecvRsp)(uint8_t *rsp, uint_fast8_t nBytes, uint32_t toTicks);
static bool (*mSdBusyWait)(uint32_t toTicks);
static enum SdHwReadResult (*mSdRecvData)(uint8_t *buf, uint_fast16_t len, uint32_t toTicks);
static void (*mSdCmdWord)(uint32_t val, uint_fast8_t nBits);
static enum SdHwWriteReply (*mSdSendData)(const uint8_t *buf, uint_fast16_t len, uint32_t toTicks);



#ifdef MINIMIZE_SIZE

	static uint_fast8_t sdPrvCrcAccount(uint8_t curCrc, uint8_t byte)
	{
	        uint_fast8_t i;

	        for (i = 0; i < 8; i++, byte <<= 1) {

	                curCrc <<= 1;

	                if ((byte ^ curCrc) & 0x80)
	                        curCrc ^= 0x09;
	        }

	        return curCrc & 0x7F;
	}

#else

	static uint_fast8_t sdPrvCrcAccount(uint8_t curCrc, uint8_t byte)
	{
		static const uint8_t crcTab7[] = {		//generated from the iterative func :)
			0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f, 0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77,
			0x19, 0x10, 0x0b, 0x02, 0x3d, 0x34, 0x2f, 0x26, 0x51, 0x58, 0x43, 0x4a, 0x75, 0x7c, 0x67, 0x6e,
			0x32, 0x3b, 0x20, 0x29, 0x16, 0x1f, 0x04, 0x0d, 0x7a, 0x73, 0x68, 0x61, 0x5e, 0x57, 0x4c, 0x45,
			0x2b, 0x22, 0x39, 0x30, 0x0f, 0x06, 0x1d, 0x14, 0x63, 0x6a, 0x71, 0x78, 0x47, 0x4e, 0x55, 0x5c,
			0x64, 0x6d, 0x76, 0x7f, 0x40, 0x49, 0x52, 0x5b, 0x2c, 0x25, 0x3e, 0x37, 0x08, 0x01, 0x1a, 0x13,
			0x7d, 0x74, 0x6f, 0x66, 0x59, 0x50, 0x4b, 0x42, 0x35, 0x3c, 0x27, 0x2e, 0x11, 0x18, 0x03, 0x0a,
			0x56, 0x5f, 0x44, 0x4d, 0x72, 0x7b, 0x60, 0x69, 0x1e, 0x17, 0x0c, 0x05, 0x3a, 0x33, 0x28, 0x21,
			0x4f, 0x46, 0x5d, 0x54, 0x6b, 0x62, 0x79, 0x70, 0x07, 0x0e, 0x15, 0x1c, 0x23, 0x2a, 0x31, 0x38,
			0x41, 0x48, 0x53, 0x5a, 0x65, 0x6c, 0x77, 0x7e, 0x09, 0x00, 0x1b, 0x12, 0x2d, 0x24, 0x3f, 0x36,
			0x58, 0x51, 0x4a, 0x43, 0x7c, 0x75, 0x6e, 0x67, 0x10, 0x19, 0x02, 0x0b, 0x34, 0x3d, 0x26, 0x2f,
			0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c, 0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04,
			0x6a, 0x63, 0x78, 0x71, 0x4e, 0x47, 0x5c, 0x55, 0x22, 0x2b, 0x30, 0x39, 0x06, 0x0f, 0x14, 0x1d,
			0x25, 0x2c, 0x37, 0x3e, 0x01, 0x08, 0x13, 0x1a, 0x6d, 0x64, 0x7f, 0x76, 0x49, 0x40, 0x5b, 0x52,
			0x3c, 0x35, 0x2e, 0x27, 0x18, 0x11, 0x0a, 0x03, 0x74, 0x7d, 0x66, 0x6f, 0x50, 0x59, 0x42, 0x4b,
			0x17, 0x1e, 0x05, 0x0c, 0x33, 0x3a, 0x21, 0x28, 0x5f, 0x56, 0x4d, 0x44, 0x7b, 0x72, 0x69, 0x60,
			0x0e, 0x07, 0x1c, 0x15, 0x2a, 0x23, 0x38, 0x31, 0x46, 0x4f, 0x54, 0x5d, 0x62, 0x6b, 0x70, 0x79,
		};

		return crcTab7[curCrc * 2 ^ byte];
	}

#endif


static uint_fast8_t sdPrvR1toSpiR1(uint32_t r1)
{
	//masks that, if matched, with sd's r1 set the bit in spi r1
	static const uint32_t masks[] = {
		0x00000000,
		0x00002000,
		0x00400000,
		0x00800000,
		0x10000000,
		0x60000000,
		0x8e000000,
		0x01398008,
	};
	uint_fast8_t ret = 0, i;
	
	for (i = 0; i < 8; i++) {
		
		if (r1 & masks[i])
			ret |= 1 << i;
	}
	
	switch ((r1 >> 9) & 0x0f) {
		case 0:
		case 8 ... 15:		//idle, disabled, undefined state sall reported as "idle"
			ret |= 0x01;
			break;
			
		case 1 ... 7:
			break;
		
		default:
			__builtin_unreachable();
	}
	
	return ret;
}

static uint32_t sdPrvReadU32BE(const uint8_t *val)
{
	uint32_t ret = 0;
	uint_fast8_t i;
	
	for (i = 0; i < 4; i++)
		ret = (ret << 8) + *val++;
	
	return ret;
}

void sdHwNotifyRCA(uint_fast16_t rca)
{
	mRCA = rca;
}

//we assume that SDMMC's clock source is same as CPU
static uint32_t sdHwPrvRecalcOneTimeout(uint_fast16_t nBytes, uint32_t nTicks)
{
	uint32_t extraBits;
	
	//convert ticks (in cpu clocks) to bits
	extraBits = nTicks / 8;	//we are unlikely to get a batter clock rate out of bit-banging than less than 8 cpcu cycles per bit

	return extraBits + 8 * nBytes;
}

static void sdHwPrvRecalcTimeouts(void)
{
	mRdTimeoutClocks = sdHwPrvRecalcOneTimeout(mTimeoutBytes, mRdTimeoutTicks);
	mWrTimeoutClocks = sdHwPrvRecalcOneTimeout(mTimeoutBytes, mWrTimeoutTicks);
}

void sdHwSetTimeouts(uint_fast16_t timeoutBytes, uint32_t rdTimeoutTicks, uint32_t wrTimeoutTicks)
{
	mTimeoutBytes = timeoutBytes;
	mRdTimeoutTicks = rdTimeoutTicks;
	mWrTimeoutTicks = wrTimeoutTicks;
	
	sdHwPrvRecalcTimeouts();
}

bool sdHwSetBusWidth(bool useFourWide)
{
	return !useFourWide;
}

static void sdPrvDelay(void)
{
	uint32_t t = mSdDelayTicks;
	
	asm volatile(
		".syntax unified		\n\t"
		"	movs %0, %1			\n\t"
		"1:						\n\t"
		"	subs %0, #1			\n\t"
		"	bne  1b				\n\t"
		:"+l"(t)
		:
		:"cc"
	);
}

static void sdPrvDebugBreak(void)
{
	#ifdef SUPPORT_LOGIC_ANALYSIS
		
		PIN_DBG_PORT->BRR = (1 << PIN_DBG_PIN);
		PIN_DBG_PORT->BSRR = (1 << PIN_DBG_PIN);

	#endif
}

static void sdPrvConfigureDataRd(void)
{
	PIN_DAT_PORT->MODER = (PIN_DAT_PORT->MODER &~ (3 << (2 * PIN_DAT_PIN))) | (0 << (2 * PIN_DAT_PIN));;		//DAT(A12) is input
}

static void sdPrvConfigureDataWr(void)
{
	PIN_DAT_PORT->MODER = (PIN_DAT_PORT->MODER &~ (3 << (2 * PIN_DAT_PIN))) | (1 << (2 * PIN_DAT_PIN));			//DAT(A12) is output
}

static void sdPrvConfigureRspRx(void)
{
	//clock is high (where it still is, data is high, where it still is)
	PIN_CMD_PORT->MODER = (PIN_CMD_PORT->MODER &~ (3 << (2 * PIN_CMD_PIN))) | (0 << (2 * PIN_CMD_PIN));			//CMD is input
}

static void sdPrvConfigureCmdTx(void)
{
	PIN_CMD_PORT->MODER = (PIN_CMD_PORT->MODER &~ (3 << (2 * PIN_CMD_PIN))) | (1 << (2 * PIN_CMD_PIN));			//CMD is output
}





static void sdPrvCmdWordSlow(uint32_t val, uint_fast8_t nBits)	//top bits sent...
{
	uint_fast8_t i;
	
	for (i = 0; i < nBits; i++, val <<= 1) {
		
		PIN_CMD_PORT->BSRR = (val & 0x80000000) ? (1 << PIN_CMD_PIN) : (1 << (16 + PIN_CMD_PIN));	//A1 to proper state
		PIN_CLK_PORT->BRR = 1 << PIN_CLK_PIN;		//clock goes low
		sdPrvDelay();
		PIN_CLK_PORT->BSRR = 1 << PIN_CLK_PIN;		//clock goes high
		sdPrvDelay();
	}
}

static inline uint_fast8_t __attribute__((always_inline)) sdPrvRspBitRxSlow(void)
{
	uint8_t ret;
	
	PIN_CLK_PORT->BRR = 1 << PIN_CLK_PIN;		//clock goes low
	sdPrvDelay();
	PIN_CLK_PORT->BSRR = 1 << PIN_CLK_PIN;		//clock goes high
	sdPrvDelay();
	ret = !!(PIN_CMD_PORT->IDR & (1 << PIN_CMD_PIN));

	return ret;
}

static inline void __attribute__((always_inline)) sdPrvDatTxBitsSlow(uint_fast8_t val, uint_fast8_t nBits)	//align high to bit 7 plz
{
	uint_fast8_t i;
	
	for (i = 0; i < nBits; i++, val <<= 1) {
		
		PIN_DAT_PORT->BSRR = (val & 0x80) ? (1 << PIN_DAT_PIN) : (1 << (16 + 12));
		PIN_CLK_PORT->BRR = 1 << PIN_CLK_PIN;		//clock goes low
		sdPrvDelay();
		PIN_CLK_PORT->BSRR = 1 << PIN_CLK_PIN;		//clock goes high
		sdPrvDelay();
	}
}

static inline uint_fast8_t __attribute__((always_inline)) sdPrvDatBitRxSlow(void)
{
	uint8_t ret;
	
	PIN_CLK_PORT->BRR = 1 << PIN_CLK_PIN;		//clock goes low
	sdPrvDelay();
	PIN_CLK_PORT->BSRR = 1 << PIN_CLK_PIN;		//clock goes high
	sdPrvDelay();
	ret = !!(PIN_DAT_PORT->IDR & (1 << PIN_DAT_PIN));

	return ret;
}

static bool sdPrvWaitRspSlow(uint8_t *rsp, uint_fast8_t nBytes, uint32_t toTicks)
{
	uint_fast8_t v, ret = 0;
	uint32_t i;
	
	for (i = 0; i < toTicks && (v = sdPrvRspBitRxSlow()); i++)
		sdPrvDebugBreak();
	
	if (i == toTicks)
		return false;
	
	for (i = 0; i < 7; i++)
		ret = ret * 2 + sdPrvRspBitRxSlow();
	*rsp++ = ret;
	
	while (--nBytes) {
		
		for (i = 0; i < 8; i++)
			ret = ret * 2 + sdPrvRspBitRxSlow();
		*rsp++ = ret;
	}
	
	return true;
}

static uint8_t sdPrvRecvByteSlow(void)
{
	uint_fast8_t i, v = 0;
	
	for (i = 0; i < 8; i++)
		v = v * 2 + sdPrvDatBitRxSlow();
	
	return v;
}

static enum SdHwReadResult sdPrvRecvDataSlow(uint8_t *dst, uint_fast16_t len, uint32_t toTicks)
{
	uint16_t crcRx, crcCalced;
	uint_fast8_t v;

	CRC->CR |= CRC_CR_RESET;
	
	while (--toTicks && sdPrvDatBitRxSlow());	//look for start bit
	
	if (!toTicks)
		return SdHwReadTimeout;
	
	sdPrvDebugBreak();
	
	while (len--) {
		
		*dst++ = v = sdPrvRecvByteSlow();
		*(volatile uint8_t*)&CRC->DR = v;
	}
	
	crcRx = sdPrvRecvByteSlow();
	crcRx <<= 8;
	crcRx += sdPrvRecvByteSlow();
	crcCalced = CRC->DR;
	
	if (!sdPrvDatBitRxSlow())	//verify end bit
		return SdHwReadFramingError;
	
	return (crcRx == crcCalced) ? SdHwReadOK : SdHwReadCrcErr;
}

static bool sdPrvBusyWaitSlow(uint32_t toTicks)
{
	//give it two ticks to get busy
	sdPrvDatBitRxSlow();
	sdPrvDatBitRxSlow();
	
	while (--toTicks && !sdPrvDatBitRxSlow());
	
	
	return !!toTicks;
}

static enum SdHwWriteReply sdPrvSendDataSlow(const uint8_t *buf, uint_fast16_t len, uint32_t toTicks)
{
	enum SdHwWriteReply ret;
	uint_fast16_t crc;
	uint_fast8_t v;
	
	CRC->CR |= CRC_CR_RESET;
	
	sdPrvDatTxBitsSlow(0x00, 1);
	sdPrvDebugBreak();
	while (len--) {
		
		v = *buf++;
		sdPrvDatTxBitsSlow(v, 8);
		*(volatile uint8_t*)&CRC->DR = v;
	}
	
	crc = CRC->DR;
	sdPrvDatTxBitsSlow(crc >> 8, 8);
	sdPrvDatTxBitsSlow(crc, 8);
	sdPrvDatTxBitsSlow(0xff, 1);
	
	sdPrvConfigureDataRd();
	//reply comes right away, top 3 bits are "turn" undriven bits and one of them is our END bit!!!!
	switch ((sdPrvRecvByteSlow() >> 1) & 0x1f) {
		case 0x05:
			ret = SdHwWriteAccepted;
			break;
		
		case 0x0b:
			ret = SdHwWriteCrcErr;
			break;
		
		case 0x0d:
			ret = SdHwWriteError;
			break;
		
		default:	//framing error
			ret = SdHwCommErr;
			break;
	}
	
	//busy wait
	
	while (--toTicks && sdPrvRecvByteSlow() != 0xff);
	
	if (toTicks || ret != SdHwWriteAccepted)
		return ret;
	
	return SdHwTimeout;
}









static void sdPrvCmdWordFast(uint32_t val, uint_fast8_t nBits)	//top bits sent...
{
	uint32_t dummy;		//C: 3.968mhz

	val = ~val;

	asm volatile(
		".syntax unified						\n\t"
		"1:										\n\t"
		"	lsrs	%0, %1, #31					\n\t"
		"	lsls	%0, %0, #4					\n\t"
		"1:										\n\t"
		"	str		%3, [%4, %0]				\n\t"
		"	str		%5, [%6, #0x28]				\n\t"
		"	lsls	%1, %1, #1					\n\t"
		"	lsrs	%0, %1, #31					\n\t"
		"	lsls	%0, %0, #4					\n\t"
		"	str		%5, [%6, #0x18]				\n\t"
		"	subs	%2, #1						\n\t"
		"	bne		1b							\n\t"
		:"=&l"(dummy), "+l"(val), "+l"(nBits)
		:"l"(1 << PIN_CMD_PIN), "l"(&PIN_CMD_PORT->BSRR), "l"(1 << PIN_CLK_PIN), "l"(PIN_CLK_PORT)
		:"memory", "cc"
	);
}

static inline uint_fast8_t __attribute__((always_inline)) sdPrvRspBitRxFast(void)
{
	uint8_t ret;
	
	ret = !!(PIN_CMD_PORT->IDR & (1 << PIN_CMD_PIN));
	PIN_CLK_PORT->BRR = 1 << PIN_CLK_PIN;		//clock goes low
	asm volatile("DSB SY");
	asm volatile("DSB SY");
	PIN_CLK_PORT->BSRR = 1 << PIN_CLK_PIN;		//clock goes high
	asm volatile("DSB SY");

	return ret;
}

static bool sdPrvWaitRspFast(uint8_t *rsp, uint_fast8_t nBytes, uint32_t toTicks)
{
	uint32_t dummy, bitCnt = 7, byteVal = 0;

	//first we wait for resp to start
	asm volatile(
		".syntax unified						\n\t"
		"1:										\n\t"
		"	nop									\n\t"
		"	nop									\n\t"
		"	nop									\n\t"
		"	nop									\n\t"
		"	ldr		%0, [%2, #0x10]				\n\t"	//->IDR
		"	str		%4, [%5, #0x28]				\n\t"	//clock goes low
		"	nop									\n\t"
		"	subs	%1, #1						\n\t"	//if we have more cycles to wait, C is set to 1, else to 0
		"	str		%4, [%5, #0x18]				\n\t"	//clock goes high
		"	tst		%0, %3						\n\t"	//if RSP is starting, sets Z to 1, else to 0
		"	bhi		1b							\n\t"	//branches if C == 1 && Z ==0, so branches if we have more cycles to wait and rsp has not yet started
		:"=&l"(dummy), "+l"(toTicks)
		:"l"(PIN_CMD_PORT), "l"(1 << PIN_CMD_PIN), "l"(1 << PIN_CLK_PIN), "l"(PIN_CLK_PORT)
		:"memory", "cc"
	);

	//on timeout, toTicks will have gone negative
	if (((int32_t)toTicks) < 0)
		return false;
	
	do {

		asm volatile(
			".syntax unified						\n\t"
			"1:										\n\t"	//byte loop
			"	nop									\n\t"
			"	nop									\n\t"
			"	nop									\n\t"
			"	nop									\n\t"
			"	ldr		%0, [%3, #0x10]				\n\t"	//->IDR
			"	str		%4, [%5, #0x28]				\n\t"	//clock goes low
			"	lsrs	%0, %6						\n\t"
			"	adcs	%2, %2						\n\t"
			"	subs	%1, #1						\n\t"	//if we have more cycles to wait, C is set to 1, else to 0
			"	str		%4, [%5, #0x18]				\n\t"	//clock goes high
			"	bne		1b							\n\t"
			:"=&l"(dummy), "+l"(bitCnt), "+l"(byteVal)
			:"l"(PIN_CMD_PORT), "l"(1 << PIN_CLK_PIN), "l"(PIN_CLK_PORT), "I"(PIN_CMD_PIN + 1)
			:"memory", "cc"
		);

		bitCnt = 8;
		*rsp++ = byteVal;


		sdPrvDebugBreak();	//first byte is 7 bits but ones after are 8 so we can capture them properly

	} while (--nBytes);
	
	return true;
}

static inline void __attribute__((always_inline)) sdPrvDatTxBitsFast(uint32_t val, uint_fast8_t nBits)	//align high to bit 7 plz
{
	uint32_t dummy;		//C: 3.968mhz

	val =~ val;

	asm volatile(
		".syntax unified						\n\t"
		"1:										\n\t"
		"	lsrs	%0, %1, #31					\n\t"
		"	lsls	%0, %0, #4					\n\t"
		"1:										\n\t"
		"	str		%3, [%4, %0]				\n\t"
		"	str		%5, [%6, #0x28]				\n\t"
		"	lsls	%1, %1, #1					\n\t"
		"	lsrs	%0, %1, #31					\n\t"
		"	lsls	%0, %0, #4					\n\t"
		"	subs	%2, #1						\n\t"
		"	str		%5, [%6, #0x18]				\n\t"
		"	bne		1b							\n\t"
		:"=&l"(dummy), "+l"(val), "+l"(nBits)
		:"l"(1 << PIN_DAT_PIN), "l"(&PIN_DAT_PORT->BSRR), "l"(1 << 8), "l"(PIN_CLK_PORT)
		:"memory", "cc"
	);
}

static inline uint_fast8_t __attribute__((always_inline)) sdPrvDatBitRxFast(void)
{
	uint8_t ret;
	
	PIN_CLK_PORT->BRR = 1 << PIN_CLK_PIN;		//clock goes low
	asm volatile("DSB SY");
	asm volatile("DSB SY");
	PIN_CLK_PORT->BSRR = 1 << PIN_CLK_PIN;		//clock goes high
	asm volatile("DSB SY");
	ret = !!(PIN_DAT_PORT->IDR & (1 << PIN_DAT_PIN));

	return ret;
}

static uint32_t sdPrvRecvManyBitsFast(uint32_t nBits)
{
	uint_fast8_t i;
	uint32_t v = 0;
	
	do {
		v = v * 2 + sdPrvDatBitRxFast();

	} while (--nBits);
	
	return v;
}

static enum SdHwWriteReply sdPrvSendDataFast(const uint8_t *buf, uint_fast16_t len, uint32_t toTicks)
{
	enum SdHwWriteReply ret;
	const uint32_t *buf32;
	uint32_t crc, v;
	
	CRC->CR |= CRC_CR_RESET;
	
	sdPrvDatTxBitsFast(0x00, 1);
	sdPrvDebugBreak();

	while (len && (((uintptr_t)buf) & 3)) {

		v = *buf++;
		sdPrvDatTxBitsFast(v << 24, 8);
		*(volatile uint8_t*)&CRC->DR = v;
		len--;
	}

	buf32 = (const uint32_t*)buf;
	while (len >= 4) {
		
		v = __builtin_bswap32(*buf32++);
		sdPrvDatTxBitsFast(v, 32);
		*(volatile uint32_t*)&CRC->DR = v;
		len -= 4;
	}

	buf = (const uint8_t*)buf32;
	while (len--) {

		v = *buf++;
		sdPrvDatTxBitsFast(v << 24, 8);
		*(volatile uint8_t*)&CRC->DR = v;
	}
	
	crc = CRC->DR;
	sdPrvDatTxBitsFast((crc << 16) + 0x8000, 17);	//CRC and then a one bit
	
	sdPrvConfigureDataRd();
	//reply comes right away, top 3 bits are "turn" undriven bits and one of them is our END bit!!!!
	switch ((sdPrvRecvManyBitsFast(8) >> 1) & 0x1f) {
		case 0x05:
			ret = SdHwWriteAccepted;
			break;
		
		case 0x0b:
			ret = SdHwWriteCrcErr;
			break;
		
		case 0x0d:
			ret = SdHwWriteError;
			break;
		
		default:	//framing error
			ret = SdHwCommErr;
			break;
	}
	
	//busy wait
	
	while (--toTicks && sdPrvRecvManyBitsFast(8) != 0xff);
	
	if (toTicks || ret != SdHwWriteAccepted)
		return ret;
	
	return SdHwTimeout;
}

static uint8_t* sdPrvRecvWordsFast(uint8_t *dst, uint32_t nWords)	//assumes nWords is nonzero, dst is aligned
{
	uint32_t dummy, dummy2;

	asm volatile(
		".syntax unified						\n\t"
		"1:								\n\t"		//wordloop
		"	movs	%0, #1				\n\t"
		"	str		%4, [%5, #0x28]		\n\t"		//clock goes low
		"	nop							\n\t"
		"	nop							\n\t"
		"2:								\n\t"		//bitloop
		"	nop							\n\t"
		"	str		%4, [%5, #0x18]		\n\t"		//clock goes high
		"	nop							\n\t"
		"	nop							\n\t"
		"	nop							\n\t"
		"	nop							\n\t"
		"	nop							\n\t"
		"	ldr		%1, [%7, #0x10]		\n\t"
		"	lsrs	%1, %1, %6			\n\t"
		"	adcs	%0, %0				\n\t"
		"	bcs		3f					\n\t"
		"	str		%4, [%5, #0x28]		\n\t"		//clock goes low
		"	b		2b					\n\t"


		"3:								\n\t"	//word done
		"	str		%0, [%8, #0x00]		\n\t"
		"	rev		%0, %0				\n\t"
		"	stmia	%3!, {%0}			\n\t"
		"	subs	%2, #1				\n\t"
		"	bne		1b					\n\t"
		:"=&l"(dummy), "=&l"(dummy2), "+l"(nWords), "+l"(dst)
		:"l"(1 << PIN_CLK_PIN), "l"(PIN_CLK_PORT), "I"(PIN_DAT_PIN + 1), "l"(PIN_DAT_PORT), "l"(CRC)
		:"memory", "cc"
	);

	return dst;
/*

	uint32_t v, dst32 = (uint32_t*)dst;

	do {
		v = sdPrvRecvManyBitsFast(32);
		*(volatile uint32_t*)&CRC->DR = v;
		*dst32++ = __builtin_bswap32(v);
	} while (--nWords);

	return (uint8_t*)dst32;
*/
}

static enum SdHwReadResult sdPrvRecvDataFast(uint8_t *dst, uint_fast16_t len, uint32_t toTicks)
{
	uint16_t crcRx, crcCalced;
	uint32_t v;

	CRC->CR |= CRC_CR_RESET;
	
	while (--toTicks && sdPrvDatBitRxFast());	//look for start bit
	
	if (!toTicks)
		return SdHwReadTimeout;
	
	sdPrvDebugBreak();
	
	while (len && (((uintptr_t)dst) & 3)) {
		
		*dst++ = v = sdPrvRecvManyBitsFast(8);
		*(volatile uint8_t*)&CRC->DR = v;
		len--;
	}

	if (len / 4) {

		dst = sdPrvRecvWordsFast(dst, len / 4);
		len %= 4;
	}

	while (len--) {
		
		*dst++ = v = sdPrvRecvManyBitsFast(8);
		*(volatile uint8_t*)&CRC->DR = v;
	}
	
	crcRx = sdPrvRecvManyBitsFast(16);
	crcCalced = CRC->DR;
	
	if (!sdPrvRecvManyBitsFast(1))	//verify end bit is high
		return SdHwReadFramingError;
	
	return (crcRx == crcCalced) ? SdHwReadOK : SdHwReadCrcErr;
}

static bool sdPrvBusyWaitFast(uint32_t toTicks)
{
	//give it two ticks to get busy
	sdPrvDatBitRxFast();
	sdPrvDatBitRxFast();
	
	while (--toTicks && !sdPrvDatBitRxFast());
	
	return !!toTicks;
}








void sdHwSetSpeed(uint32_t maxSpeed)
{
	//we just have two speeds - slow and fast
	if (maxSpeed <= 400000) {
		
		mSdRecvRsp = sdPrvWaitRspSlow;
		mSdRecvData = sdPrvRecvDataSlow;
		mSdCmdWord = sdPrvCmdWordSlow;
		mSdBusyWait = sdPrvBusyWaitSlow;
		mSdSendData = sdPrvSendDataSlow;
	}
	else {
		mSdRecvRsp = sdPrvWaitRspFast;
		mSdRecvData = sdPrvRecvDataFast;
		mSdCmdWord = sdPrvCmdWordFast;
		mSdBusyWait = sdPrvBusyWaitFast;
		mSdSendData = sdPrvSendDataFast;
	}
	
	sdHwPrvRecalcTimeouts();
}

static void sdHwPrvEnd(void)
{
	PIN_DAT_PORT->MODER = (PIN_DAT_PORT->MODER &~ (3 << (2 * PIN_DAT_PIN))) | (2 << (2 * PIN_DAT_PIN));		//DAT(A12) is output
	PIN_CMD_PORT->MODER = (PIN_CMD_PORT->MODER &~ (3 << (2 * PIN_CMD_PIN))) | (2 << (2 * PIN_CMD_PIN));		//CMD is AFR
}


uint32_t sdHwInit(void)
{
	//calc the delay
	mSdDelayTicks = TICKS_PER_SECOND / (300000 * 6);

	//default timeouts
	sdHwSetSpeed(400000);
	sdHwSetTimeouts(10000, 0, 0);
	
	//crc unit
	CRC->INIT = 0;
	CRC->POL = 0x1021;
	CRC->CR = CRC_CR_POLYSIZE_0;

	//CMD pin
	sdPrvConfigureRspRx();																					//CMD is input

	//CLK pin is B8 and already an output, high

	//DAT pin
	sdPrvConfigureDataRd();																					//DAT is input

	#ifdef SUPPORT_LOGIC_ANALYSIS

		//SYNC pin
		PIN_DBG_PORT->BRR = 1 << PIN_DBG_PIN;																//low
		PIN_DBG_PORT->MODER = (PIN_DBG_PORT->MODER &~ (3 << (2 * PIN_DBG_PIN))) | (1 << (2 * PIN_DBG_PIN));	//output

	#endif

	return SD_HW_FLAG_INITED | SD_HW_FLAG_SDIO_IFACE;
}

enum SdHwCmdResult sdHwCmd(uint_fast8_t cmd, uint32_t param, bool cmdCrc, enum SdHwRespType respTyp, void *respBufOut, enum SdHwDataDir dataDir, uint_fast16_t blockSz, uint32_t numBlocks)
{
	uint8_t rawResp[17], *respOut = rawResp;
	uint_fast8_t crc = 0, rb, crcRxed;
	enum SdHwCmdResult res;
	bool gotResp;
	
	
	(void)cmdCrc;
	(void)dataDir;
	(void)blockSz;
	(void)numBlocks;
	
	crc = sdPrvCrcAccount(crc, 0x40 | cmd);
	crc = sdPrvCrcAccount(crc, param >> 24);
	crc = sdPrvCrcAccount(crc, param >> 16);
	crc = sdPrvCrcAccount(crc, param >> 8);
	crc = sdPrvCrcAccount(crc, param);
	crc = crc * 2 + 1;
	
	sdPrvConfigureCmdTx();
	if (dataDir != SdHwDataNone) {

		sdPrvConfigureDataWr();
		PIN_DAT_PORT->BSRR = (1 << PIN_DAT_PIN);	//dat is high by default
		sdPrvConfigureDataRd();
		while (!(PIN_DAT_PORT->IDR & (1 << PIN_DAT_PIN)));
	}
	else {

		sdPrvConfigureDataRd();
	}

	sdPrvDebugBreak();
	mSdCmdWord(0xffffff40 + cmd, 32);
	mSdCmdWord(param, 32);
	mSdCmdWord(((uint32_t)crc) << 24, 8);
	sdPrvConfigureRspRx();
	
	gotResp = mSdRecvRsp(respOut, (respTyp == SdRespTypeSdR2) ? 17 : 6, 128);
	if (!gotResp) {
		
		res = (respTyp == SdRespTypeNone) ? SdHwCmdResultOK : SdHwCmdResultRespTimeout;
		goto done;
	}
	
	if (respOut[0] & 0xc0) {		//always
		
		res = SdCmdInternalError;		//framing error
		goto done;
	}
	
	if (respTyp == SdRespTypeSdR2) {
		
		if (*respOut++ != 0x3f) {
			
			res = SdCmdInternalError;	//resp to something else?
			goto done;
		}
			
		for (rb = 0, crc = 0; rb < 15; rb++)
			crc = sdPrvCrcAccount(crc, *respOut++);
		
		crcRxed = *respOut++;
	}
	else {
		
		for (rb = 0, crc = 0; rb < 5; rb++)
			crc = sdPrvCrcAccount(crc, *respOut++);
			
		crcRxed = *respOut++;
	}
	crc = crc * 2 + 1;	//so we can compare it to the RXed value
	
	switch (respTyp) {
		
		case SdRespTypeR1:
		case SdRespTypeR1withBusy:
			if (rawResp[0] != cmd) {
				
				res = SdCmdInternalError;	//resp to something else?
				goto done;
			}
			*(uint8_t*)respBufOut = sdPrvR1toSpiR1(sdPrvReadU32BE(rawResp + 1));
			break;
		
		case SdRespTypeR3:
			//make crc match
			crcRxed = crc;
			for (rb = 0; rb < 4; rb++)
				((uint8_t*)respBufOut)[rb] = rawResp[rb + 1];
			break;
			
			
		case SdRespTypeR7:
		case SdRespTypeSdR6:
			if (rawResp[0] != cmd) {
				
				res = SdCmdInternalError;	//resp to something else?
				goto done;
			}
			for (rb = 0; rb < 4; rb++)
				((uint8_t*)respBufOut)[rb] = rawResp[rb + 1];
			break;
		
		case SdRespTypeSdR2:
			for (rb = 0; rb < 16; rb++)
				((uint8_t*)respBufOut)[rb] = rawResp[rb + 1];
			break;
		
		default:
			__builtin_unreachable();
			break;
	}
	
	res = (crcRxed == crc) ? SdHwCmdResultOK : SdCmdInternalError;
	
	if (res == SdHwCmdResultOK && respTyp == SdRespTypeR1withBusy) {
		
		if (!mSdBusyWait(100000))
			res = SdCmdInternalError;
	}

done:
	mSdCmdWord(0xffffffff, 8);	//give card the necessary 8 clocks to finish shit
	
	//if we're done, deconfigure pins
	if (dataDir == SdHwDataNone || res != SdHwCmdResultOK)
		sdHwPrvEnd();

	return res;
}

enum SdHwReadResult sdHwReadData(uint8_t *data, uint_fast16_t sz)	//length must be even, pointer must be halfword aligned
{
	sdPrvConfigureDataRd();
	return mSdRecvData(data, sz, mRdTimeoutClocks);
}

enum SdHwWriteReply sdHwWriteData(const uint8_t *data, uint_fast16_t sz, bool isMultiblock)
{
	sdPrvConfigureDataWr();
	return mSdSendData(data, sz, mWrTimeoutClocks);
}

bool sdHwPrgBusyWait(void)
{
	return true;
}

void sdHwRxRawBytes(void *dstP, uint_fast16_t numBytes)
{
	
}

bool sdHwMultiBlockWriteSignalEnd(void)
{
	return true;
}

bool sdHwMultiBlockReadSignalEnd(void)
{
	return true;
}

void sdHwGiveInitClocks(void)
{
	uint_fast8_t i;
	
	for (i = 0; i < 4; i++)
		mSdCmdWord(0xffffffff, 32);
}

void sdHwChipDeselect(void)
{
	sdPrvConfigureCmdTx();
	sdPrvDebugBreak();
	mSdCmdWord(0xffffffff, 32);	//last clocks after deselect. docs say 8, we give 32 just in case
	sdHwPrvEnd();
}
