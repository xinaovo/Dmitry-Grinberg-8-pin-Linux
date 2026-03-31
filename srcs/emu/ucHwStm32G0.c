/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "stm32g031xx.h"
#include "timebase.h"
#include "printf.h"
#include "ucHw.h"

//SD.MISO = A3(10)			spi2.miso
//SD.MOSI = A4(11)			spi2.mosi
//SD.SCLK = A0(7)			spi2.sck
//SD.NCS = B0/B1/B2/A8(15)

//RAM.MISO = A6(13)			spi1.miso
//RAM.MOSI = A2(9)			spi1.mosi
//RAM.SCLK = A1(8)			spi1.sck
//RAM.nCS1 = A5(12)
//RAM.nCS2 = A7(14)
//RAM.nCS3 = A11(16)
//RAM.nCS4 = A12(17)

//UART.DO = B3/B4/B5/B6(20)	usart1.tx(b6)
//UART.DI = B7/B8(1)		usart1.rx(b7)


//idea (if SD accepts mode 3 spi):
//RAM uses SPI (miso, mosi, sck, ram_nCS)
//SD uses SDIO, where CLK = ram_ncs, CMD = sck, DAT = mosi
//this frees a pin that would be SD nCS
//card will keep seing a lot of zeroes which are invalid commands. it will not reply, which is fine
//we might need to begin each command with 64 clocks of 0xff to clear up internal state
//or maybe psram can use mode3 and thus card will keep seeing high?
//this removed need for low pass filtering
//alternatively, select can be inverted between sd and card, at least one always selected :)
//that needs a fet but maybe we can use one

//use STM TRNG for random instead of bothering to keep up with it?

//use more of the ram - there is a lot

//number of wired entries should be 0, random reg shdoul be optimized

//xcheck if usb perupheral is available anyways and if so, if its ram is avail. 2K is 2K
//same for FDCAN:
//0x4000 B400  2KB FDCAN message RAM  (docs are conflicted on actual amount avail here, 0.8KB, 1KB, 2KB)
//0x4000 9C00 1 KB USB RAM2
//0x4000 9800 1 KB USB RAM1

//xxxx: above


//RAM_nCS = SD_CLK
//RAM_CLK = SD_CMD
//RAM_MOSI = SD_DAT




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
		//RAM_CLK = SD_CMD
		//RAM_MOSI = SD_DAT(PA10)


		data READ can be done using spi2 :)



	//final fina final map:

	(1) B8: SDIO_CLK, RAM_NCS
	(4) A1: SDIO_CMD, RAM_CLK
	(5) A11: RAM_MISO
	(5) A9: USART1_TX (flash mode only)
	(6) A12: SDIO_DAT, RAM_MOSI
	(6) A10: USART1_RX (flash mode only)
	(7) A13: SWDIO (BBUART_TX - prod use)
	(8) A14: SWCLK
	(8) A15: USART2_RX (prod use)


*/

void initHwSuperEarly(void)
{
	
}

void hwError(uint_fast8_t err)
{
	
}




void initHw(void)
{
	uint32_t flashLatency = (TICKS_PER_SECOND / 30000000);	//25% over spec
	
	//enable clocks to important places
	RCC->AHBENR = RCC_AHBENR_FLASHEN | RCC_AHBENR_DMA1EN | RCC_AHBENR_CRCEN;
	RCC->APBENR1 = RCC_APBENR1_DBGEN | RCC_APBENR1_PWREN | RCC_APBENR1_SPI2EN | RCC_APBENR1_USART2EN;
	RCC->APBENR2 = RCC_APBENR2_SYSCFGEN | RCC_APBENR2_SPI1EN;
	RCC->IOPENR = RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN | RCC_IOPENR_GPIODEN | RCC_IOPENR_GPIOFEN;
	
	//setup flash
	if (flashLatency > 7)	//max
		flashLatency = 7;

	FLASH->ACR = FLASH_ACR_DBG_SWEN;
	FLASH->ACR = FLASH_ACR_DBG_SWEN | FLASH_ACR_ICRST;
	FLASH->ACR = FLASH_ACR_DBG_SWEN | FLASH_ACR_ICEN | FLASH_ACR_PRFTEN | (flashLatency << FLASH_ACR_LATENCY_Pos);
	
	//voltage scaling
	PWR->CR1 = 0;	//STM docs say VOS0 no longer exixts...it does...and it is thr secret to speeds above 100MHz
	while (PWR->SR2 & PWR_SR2_VOSF);
	
	//set up all AHBs and APBs with no division, use HSI (16MHz)
	RCC->CFGR = 0;
	
	//turn off the PLL
	RCC->CR &=~ RCC_CR_PLLON;
	while (RCC->CR & RCC_CR_PLLRDY);
	//start PLL (input = HSI / 2 = 8MHz), VCO output TICKS_PER_SECOND * 2, make it output TICKS_PER_SECOND Hz on output "R", nothing elsewhere
	RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI | RCC_PLLCFGR_PLLM_0 | ((TICKS_PER_SECOND / 4000000) << RCC_PLLCFGR_PLLN_Pos) | RCC_PLLCFGR_PLLREN | RCC_PLLCFGR_PLLR_0;
	//turn it on
	RCC->CR |= RCC_CR_PLLON;
	//wait for it
	while (!(RCC->CR & RCC_CR_PLLRDY));
	//go to it
	RCC->CFGR = RCC_CFGR_SW_1;

	//this mess compiles to tight code
	#define PULL_NONE		0
	#define PULL_UP			1
	#define PULL_DOWN		2

	#define PORT_CFG(_name)																\
		{																				\
			GPIO_TypeDef *_g =  _name;													\
			uint32_t mask1b = 0, val = 0, mode = 0, speed = 0, pupdr = 0, mask2b = 0;	\
			uint64_t mask4b = 0, afr = 0;

	#define PORT_CFG_END																\
			if (mask1b)																	\
				_g->BSRR = (mask1b & val) | ((mask1b &~ val) << 16);					\
			if (mask2b) {																\
				_g->MODER = (_g->MODER &~ mask2b) | (mode & mask2b);					\
				_g->PUPDR = (_g->PUPDR &~ mask2b) | (pupdr & mask2b);					\
				_g->OSPEEDR = (_g->OSPEEDR &~ mask2b) | (speed & mask2b);				\
			}																			\
			if (mask4b & 0x00000000ffffffffull)											\
				_g->AFR[0] = (_g->AFR[0] &~ mask4b) | (afr & mask4b);					\
			if (mask4b & 0xffffffff00000000ull)											\
				_g->AFR[1] = (_g->AFR[1] &~ (mask4b >> 32)) | ((afr & mask4b) >> 32);	\
		}

	#define PIN_CFG(_no, _afr, _speed, _mode, _pull, _val)								\
		do {																			\
			uint32_t _n = _no, _m1 = 1 << _n, _m2 = 3 << (_n * 2);						\
			int32_t _v = _val, _a = _afr;												\
			if (_v >= 0) {																\
				mask1b |= _m1;															\
				val |= _v << _n;														\
			}																			\
			mask2b |= _m2;																\
			mode = (mode &~ _m2) | (((_mode) << (_n * 2)) & _m2);						\
			pupdr = (pupdr &~ _m2) | (((_pull) << (_n * 2)) & _m2);						\
			speed = (speed &~ _m2) | (((_speed) << (_n * 2)) & _m2);					\
			if (_a >= 0) {																\
				uint64_t _m4 = 0x0full << (_n * 4);										\
				mask4b |= _m4;															\
				afr = (afr &~ _m4) | ((((uint64_t)_a) << (_n * 4)) & _m4);				\
			}																			\
		} while (0)



	#define PIN_CFG_AFR(_no, _afr, _speed, _pull)	PIN_CFG(_no, _afr, _speed, 2, _pull, -1)
	#define PIN_CFG_IN(_no, _speed, _pull)			PIN_CFG(_no, -1, _speed, 0, _pull, -1)
	#define PIN_CFG_OUT(_no, _speed, _val)			PIN_CFG(_no, -1, _speed, 1, PULL_NONE, _val)

	PORT_CFG(GPIOA)
		PIN_CFG_AFR(1, 0, 3, PULL_UP);			//A1 = RAM.SSCK/SD.CMD high speed, pulled up
		PIN_CFG_AFR(11, 0, 3, PULL_UP);			//A11 = RAM.MISO, high speed, pulled up
		PIN_CFG_AFR(12, 0, 3, PULL_UP);			//A12 = RAM.MOSI/SD.DAT, high speed, pulled up

		PIN_CFG_OUT(13, 1, 1);					//A13 for console out (usually SWDIO)
		PIN_CFG_AFR(14, 1, 2, PULL_UP);			//A14 = USART2.TX whicxh is used as RX due to "swap"
	PORT_CFG_END

	PORT_CFG(GPIOB)
		PIN_CFG_OUT(8, 1, 1);					//B8 = RAM.nCS/SD.CLK = gpio out high, low speed
	PORT_CFG_END
}
