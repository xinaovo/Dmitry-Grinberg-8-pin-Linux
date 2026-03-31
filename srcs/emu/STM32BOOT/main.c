/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "stm32g031xx.h"
#include <string.h>
#include "timebase.h"
#include "printf.h"
#include "usart.h"
#include "ufat.h"
#include "sd.h"


#define APP_VER_OFFSET				0x10

#define FLASH_BLOCK_SIZE			2048	//erase size, necessarily a multiple of 512


extern uint32_t __bl_start[], __app_start[];
static uint8_t __attribute__((aligned(4))) mBuf[512];
static uint32_t mSec = 0xffffffff;
static int16_t mClockReq = -1;

void prPutchar(char chr)
{
	/*volatile uint32_t *addr = (volatile uint32_t*)ZWT_ADDR;
	uint32_t timeout = 100000;
			
	while (addr[0] & 0x80000000ul) {
		if (!--timeout)
			break;
	}
	addr[0] = 0x80000000ul | (uint8_t)chr;
	*/
	if (chr == '\n')
		usartTx('\r');
	usartTx(chr);
}


bool ufatExtRead(uint32_t sector, uint16_t offset, uint8_t len, uint8_t* buf)
{
	const volatile uint8_t *from;
	
	if (sector != mSec && !sdSecRead(sector, mBuf))
		return false;
	mSec = sector;
	
	from = mBuf + offset;
	while (len--)
		*buf++ = *from++;
	
	return true;
}

static bool findUpdateFile(uint32_t *sizeP, uint32_t maxSize)	//the name is false - it also looks for clocking file names
{
	int32_t updateID = -1;
	uint16_t n = 0, id;
	uint8_t flags;
	char name[12];
	uint32_t sz;
	
	for (n = 0; ufatGetNthFile(n, name, &sz, &flags, &id); n++) {

		if (flags & (UFAT_FLAG_VOLUME_LBL | UFAT_FLAG_DIR))
			continue;

		pr(" > '%s' %08xh bytes %02xh flags @ 0x%04xh\n", name, sz, flags, id);
		
		if (!memcmp(name, "CLOCK", 5)) {
			uint_fast16_t clk = 0, i;

			for (i = 5; i < 8; i++) {
				if (name[i] >= '0' && name[i] <= '9')
					clk = clk * 10 + name[i] - '0';
				else
					break;
			}
			if (i != 5)	//only assign if there was at least one char
				mClockReq = clk;
		}

		if (!memcmp(name, "FIRMWAREBIN", sizeof(name))) {

			if (sz > 1024 && sz < maxSize) {
			
				updateID = (uint32_t)id;
				if (sizeP)
					*sizeP = sz;
			}
			else {

				pr("Size %u is not valid (max %u)\n", sz, maxSize);
			}
		}
	}
	
	return updateID >= 0 && ufatOpen(updateID);
}

static void hwInit(void)
{
	RCC->AHBENR = RCC_AHBENR_FLASHEN | RCC_AHBENR_CRCEN;
	RCC->APBENR1 = RCC_APBENR1_DBGEN | RCC_APBENR1_PWREN;
	RCC->APBENR2 = RCC_APBENR2_SYSCFGEN;
	RCC->IOPENR = RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN | RCC_IOPENR_GPIODEN | RCC_IOPENR_GPIOFEN;

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
			uint64_t _m4 = 0x0full << (_n * 4);											\
			int32_t _v = _val;															\
			if (_v >= 0) {																\
				mask1b |= _m1;															\
				val |= _v << _n;														\
			}																			\
			mask2b |= _m2;																\
			mode = (mode &~ _m2) | (((_mode) << (_n * 2)) & _m2);						\
			pupdr = (pupdr &~ _m2) | (((_pull) << (_n * 2)) & _m2);						\
			speed = (speed &~ _m2) | (((_speed) << (_n * 2)) & _m2);					\
			mask4b |= _m4;																\
			afr = (afr &~ _m4) | (((_afr) << (_n * 4)) & _m4);							\
		} while (0)



	#define PIN_CFG_AFR(_no, _afr, _speed, _pull)	PIN_CFG(_no, _afr, _speed, 2, _pull, -1)
	#define PIN_CFG_IN(_no, _speed, _pull)			PIN_CFG(_no, 0, _speed, 0, _pull, -1)
	#define PIN_CFG_OUT(_no, _speed, _val)			PIN_CFG(_no, 0, _speed, 1, PULL_NONE, _val)

	PORT_CFG(GPIOA)
		PIN_CFG_AFR(1, 0, 3, PULL_UP);			//A1 = RAM.SSCK/SD.CMD high speed, pulled up
		PIN_CFG_AFR(12, 0, 3, PULL_UP);			//A12 = RAM.MOSI/SD.DAT, high speed, pulled up

		PIN_CFG_OUT(13, 1, 1);					//A13 for console out
	//	PIN_CFG_OUT(11, 1, 1);					//A11 for console out

	PORT_CFG_END

	PORT_CFG(GPIOB)
		PIN_CFG_OUT(8, 1, 1);					//B8 = RAM.nCS/SD.CLK = gpio out high, low speed
	PORT_CFG_END
}


static void __attribute__((naked, section(".data"))) flashPrvSimpleGoAsm(FLASH_TypeDef *f, uint32_t crWriteVal)
{
	asm volatile(
		".syntax unified				\n\t"
		"	cpsid	i					\n\t"
		"	str		r1, [r0, #0x14]		\n\t"
		"1:								\n\t"
		"	ldr		r1, [r0, #0x10]		\n\t"
		"	lsrs	r1, r1, %0			\n\t"
		"	bcs		1b					\n\t"
		"	cpsie	i					\n\t"
		"	bx		lr					\n\t"
		:
		:"i"(FLASH_SR_BSY1_Pos + 1)
		:"memory", "cc"
	);
}


static void __attribute__((naked, section(".data"))) flashPrvWriteGoAsm(FLASH_TypeDef *f, volatile uint32_t *dst, const uint32_t *src)
{
	asm volatile(
		".syntax unified				\n\t"
		"	cpsid	i					\n\t"
		"	ldmia	r2, {r2, r3}		\n\t"
		"	stmia	r1!, {r2}			\n\t"	//order matters...
		"	stmia	r1!, {r3}			\n\t"
		"1:								\n\t"
		"	ldr		r2, [r0, #0x10]		\n\t"
		"	lsrs	r2, r2, %0			\n\t"
		"	bcs		1b					\n\t"
		"	cpsie	i					\n\t"
		"	bx		lr					\n\t"
		:
		:"i"(FLASH_SR_BSY1_Pos + 1)
		:"memory", "cc"
	);
}

static void __attribute__((naked)) flashPrvSimpleGo(FLASH_TypeDef *f, uint32_t crWriteVal)							//a better veneer than gcc will use
{
	asm volatile(
		"ldr	r3, =%0	\n\t"
		"bx		r3		\n\t"
		:
		:"i"(&flashPrvSimpleGoAsm)
		:"memory", "cc"
	);
}

static void __attribute__((naked)) flashPrvWriteGo(FLASH_TypeDef *f, volatile uint32_t *dst, const uint32_t *src)		//a better veneer than gcc will use
{
	asm volatile(
		"ldr	r3, =%0	\n\t"
		"bx		r3		\n\t"
		:
		:"i"(&flashPrvWriteGoAsm)
	);
}


static void flashPrvUnlockFlash(void)
{
	if (FLASH->CR & FLASH_CR_LOCK_Msk) {

		FLASH->KEYR = 0x45670123;
		FLASH->KEYR = 0xcdef89ab;

		if (FLASH->CR & FLASH_CR_LOCK_Msk) {

			pr("failed to unlock flash\n");
			return;
		}
	}
}

static void flashPrvUnlockOptionBytes(void)
{
	if (FLASH->CR & FLASH_CR_OPTLOCK_Msk) {

		FLASH->OPTKEYR = 0x08192a3b;
		FLASH->OPTKEYR = 0x4c5d6e7f;

		if (FLASH->CR & FLASH_CR_OPTLOCK_Msk) {

			pr("failed to unlock options\n");
			return;
		}
	}
}

static void flashPrvLock(void)
{
	FLASH->CR |= FLASH_CR_LOCK_Msk;
}

static void flashSetProperFuses(void)
{
	const uint32_t optionWordMask = 0x1b000100, optionWordDesiredMaskedVal = 0x12000000;
	volatile uint32_t *optionWord = (volatile uint32_t*)0x1FFF7800;
	uint32_t curVal = *optionWord, desiredVal = (curVal &~ optionWordMask) | optionWordDesiredMaskedVal;

	if (curVal != desiredVal) {

		pr("FLASH option word is 0x%08x, desired is 0x%08x\n", curVal, desiredVal);

		flashPrvUnlockOptionBytes();
		
		FLASH->OPTR = desiredVal;
		FLASH->SR = -1;
		flashPrvSimpleGo(FLASH, FLASH->CR | FLASH_CR_OPTSTRT_Msk);
		FLASH->SR = -1;
		FLASH->CR |= FLASH_CR_OBL_LAUNCH_Msk;
		while (FLASH->CR & FLASH_CR_OBL_LAUNCH_Msk);
		pr("flash option bytes programmed\n");
	}
}

static bool flashPrvErzBlock(uint32_t addr)
{
	uint32_t crVal, pnb = (addr - FLASH_BASE) / FLASH_BLOCK_SIZE;


	FLASH->SR = -1;
	FLASH->CR = crVal = (FLASH->CR &~ FLASH_CR_PNB_Msk) | (pnb << FLASH_CR_PNB_Pos) | FLASH_CR_PER;

	flashPrvSimpleGo(FLASH, crVal | FLASH_CR_STRT);
	FLASH->CR &=~ FLASH_CR_PER;
	
	return (FLASH->SR & (FLASH_SR_MISERR | FLASH_SR_PGSERR | FLASH_SR_SIZERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR | FLASH_SR_PROGERR | FLASH_SR_OPERR)) == 0;
}

static bool flashPrvWritePage(uint32_t addr, const uint8_t *srcP, uint32_t size)	//assumes src is 32-bit aligned
{
	volatile uint32_t *dst = (volatile uint32_t*)addr;
	const uint32_t *src = (const uint32_t*)srcP;
	uint32_t i;
	
	if (size % 8 || addr % 8)
		return false;

	FLASH->SR = -1;
	FLASH->CR |= FLASH_CR_PG;
	for (i = 0; i < size / 8; i++, dst += 2, src += 2)
		flashPrvWriteGo(FLASH, dst, src);
	FLASH->CR &=~ FLASH_CR_PG;
	
	return (FLASH->SR & (FLASH_SR_MISERR | FLASH_SR_PGSERR | FLASH_SR_SIZERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR | FLASH_SR_PROGERR | FLASH_SR_OPERR)) == 0;
}

static bool performUpdate(uint32_t flashAddr, uint32_t updateSize)
{
	uint32_t flashOfst = 0, secIdx = 0, numSec = 0, firstSecSize = 0;
	int32_t firstSec = -1;

	if (!updateSize) {
		pr(" no update given\n");
		return false;
	}

	while (updateSize) {

		uint32_t now = updateSize > sizeof(mBuf) ? sizeof(mBuf) : updateSize;

		if (!numSec) {						//current sector range is used up - we need a new one
			
			if (!ufatGetNextSectorRange(&secIdx, &numSec)) {
				
				pr("cannot get next sector range\n");
				return false;
			}
		}

		//pr(" sec %u+%u -> 0x%08x (%u bytes left)\n", secIdx, numSec, flashAddr + flashOfst, updateSize);
		mSec = 0xffffffff;		//we are reusing the buffer, invalidate it...
		if (!sdSecRead(secIdx, mBuf)) {
						
			pr("cannot read sector %u\n", secIdx);
			return false;
		}

		if (!flashOfst) {

			int32_t curVer = *(volatile uint32_t*)(flashAddr + APP_VER_OFFSET);
			int32_t proposedVer = *(volatile uint32_t*)(mBuf + APP_VER_OFFSET);

			if (curVer >= proposedVer) {

				pr("Current app version is %d, update is %d. %sing...\n", curVer, proposedVer, "Skipp");
				return true;
			}

			pr("Current app version is %d, update is %d. %sing...\n", curVer, proposedVer, "Updat");
			firstSecSize = now;
			firstSec = secIdx;
		}
		
		if ((flashAddr + flashOfst - FLASH_BASE) % FLASH_BLOCK_SIZE == 0) {

			pr(" erz 0x%08x\n", flashAddr + flashOfst);
			
			if (!flashPrvErzBlock(flashAddr + flashOfst)) {
			
				pr("Failed to erase block at 0x%08x: 0x%08x\n", flashAddr + flashOfst, FLASH->SR);
				return false;
			}
		}

		if (flashOfst) {

			mSec = 0xffffffff;		//we are reusing the buffer, invalidate it...
			memset(mBuf + now, 0x00, sizeof(mBuf) - now);
			if (!flashPrvWritePage(flashAddr + flashOfst, (void*)mBuf, sizeof(mBuf))) {
				
				pr("Failed to write page at 0x%08x: 0x%08x\n", flashAddr + flashOfst, FLASH->SR);
				return false;
			}
		}
		secIdx++;
		numSec--;
		flashOfst += now;
		updateSize -= now;
	}

	//now the first sec
	//pr(" sec %u -> 0x%08x (%u bytes left)\n", firstSec, flashAddr, 0);
	mSec = 0xffffffff;		//we are reusing the buffer, invalidate it...
	if (!sdSecRead(firstSec, mBuf)) {
					
		pr("cannot read sector %u\n", firstSec);
		return false;
	}
	memset(mBuf + firstSecSize, 0x00, sizeof(mBuf) - firstSecSize);
	if (!flashPrvWritePage(flashAddr, (void*)mBuf, sizeof(mBuf))) {
		
		pr("Failed to write page at 0x%08x\n", flashAddr + flashOfst);
		return false;
	}

	return true;
}

static void tryPerformUpdate(void)
{
	const uint32_t flashSz = FLASH_SIZE, appBase = (uint32_t)__app_start, appSizeMax = flashSz - (appBase - FLASH_BASE);
	const uint32_t appBlocksMax = appSizeMax / FLASH_BLOCK_SIZE;
	uint32_t updateFileSz = 0;
	uint8_t buf[64];
		
	if (!sdCardInit(buf)) {
		
		pr("SD init fail\n");
	}
	else if (!ufatMount()) {
		
		pr("mount fail\n");
	}
	else if (!findUpdateFile(&updateFileSz, appSizeMax)) {
		
		pr("cannot find or open update file\n");
	}
	else if (!performUpdate(appBase, updateFileSz)) {
		
		pr("cannot perform update\n");
	}
	else {
		
		pr("update performed\n");
	}
}


static bool isAppValid(void)
{
	uint32_t allegedSP = __app_start[0], allegedPC = __app_start[1];
	uint32_t flashSz = FLASH_SIZE;
	uint32_t ramSz = SRAM_SIZE_MAX;
	
	if (allegedSP % 4)														//SP should be word aligned;
		return false;
	if (!(allegedPC % 2))													//PC must be odd
		return false;
	if (allegedSP <= 0x20000000 || allegedSP > (0x20000000 + ramSz))		//SP must be in RAM
		return false;
	if (allegedPC < FLASH_BASE + 8 || allegedPC - FLASH_BASE >= flashSz)	//PC must be in flash
		return false;
	
	return true;
}

static void __attribute__((naked)) doBoot(uintptr_t param)
{
	asm volatile(
		"	ldr		r1, =%0			\n\t"
		"	ldr		r2, =%1			\n\t"
		"	cpsid	i				\n\t"
		"	str		r1, [r2]		\n\t"
		"	ldmia	r1, {r1, r2}	\n\t"
		"	mov		sp, r1			\n\t"
		"	mov		pc, r2			\n\t"
		:
		:"i"(__app_start), "i"(&SCB->VTOR)
		:"memory"
	);
}

void __attribute__((noreturn, used)) micromain(void)
{
	uint64_t time;
	
	timebaseInit();
	time = getTime();
	while (getTime() - time < 7 * TICKS_PER_SECOND);
	
	//XXX: this next part disables SWD.... make sure the delay is before it....
	hwInit();
	usartSecondaryInit();

	flashPrvUnlockFlash();
	flashSetProperFuses();
	tryPerformUpdate();
	flashPrvLock();
	timebaseDeinit();

	if (isAppValid()) {

		uint32_t clock = 128;

		if (mClockReq < 0) {

			//nothing
		}
		else if (mClockReq < 32 || mClockReq > 200) {

			pr("clock request of %d MHZ is not credible\n", mClockReq);
		}
		else {

			clock = mClockReq;
		}

		pr("booting at %u MHz...\n", clock);
		doBoot(clock * 1000000);
	}
	else {

		pr("no valid firmware found\n");
	}
	while (1);
}


void __attribute__((used)) report_hard_fault(uint32_t* regs, uint32_t ret_lr, uint32_t *user_sp)
{
	uint32_t *push = (ret_lr == 0xFFFFFFFD) ? user_sp : (regs + 8), *sp = push + 8;
	unsigned i;
	
	prRaw("== HARDFAULT ==\n");
	for (i = 0; i < 4; i++)
		prRaw("R%02xh=0x%08x\n", i, (unsigned)*push++);
	for (i = 0; i < 8; i++)
		prRaw("R%02xh=0x%08x\n", i + 4, (unsigned)*regs++);
	prRaw("R%02xh=0x%08x\n", 12, (unsigned)*push++);
	prRaw("R%02xh=0x%08x\n", 13, (unsigned)sp);
	for (i = 0; i < 3; i++)
		prRaw("R%02xh=0x%08x\n", i + 14, (unsigned)*push++);		//R16 is SR
	prRaw("== HARDFAULT ==\n");
	while (1);
}

void __attribute__((noreturn, naked, noinline)) HardFault_Handler(void)
{
	asm volatile(
			"mov  r0, r8				\n\t"
			"mov  r1, r9				\n\t"
			"mov  r2, r10				\n\t"
			"mov  r3, r11				\n\t"
			"push {r0-r3}				\n\t"
			"push {r4-r7}				\n\t"
			"mov  r0, sp				\n\t"
			"mov  r1, lr				\n\t"
			"mrs  r2, PSP				\n\t"
			"bl   report_hard_fault		\n\t"
			:::"memory");
}
