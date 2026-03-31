/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#if defined(CPU_STM32G0)
	#include "stm32g031xx.h"
#elif defined(CPU_SAMD)
	#include "samda1e16b.h"
#elif defined(CPU_CORTEXEMU)
	#include "CortexEmuCpu.h"
#elif defined(CPU_RP2350)
	#include "RP2350.h"
#endif

#include <string.h>
#include "../hypercall.h"
#include "timebase.h"
#include "spiRam.h"
#include "printf.h"
#include "decBus.h"
#include "ds1287.h"
#include "usart.h"
#include "ucHw.h"
#include "sdHw.h"
#include "dz11.h"
#include "soc.h"
#include "mem.h"
#include "sd.h"


static uint8_t __attribute__((aligned(4))) mDiskBuf[SD_BLOCK_SIZE];
static uint32_t mCurrentTickRate;

#ifdef ULTRIX_SUPPORT
	#error "no ultrix"
#endif

#ifdef MMIO_RAM_ADDR
	
	#error "no mmio ram"

#endif



void delayMsec(uint32_t msec)
{
	uint64_t till = getTime() + (uint64_t)msec * (TICKS_PER_SECOND / 1000);
	
	while (getTime() < till);
}


void prPutchar(char chr)
{
	#ifdef SUPPORT_DEBUG_PRINTF
	
		#if (ZWT_ADDR & 3)		//byte
		
			*(volatile uint8_t*)ZWT_ADDR = chr;
					
		#else
			volatile uint32_t *addr = (volatile uint32_t*)ZWT_ADDR;
			
//			while(addr[0] & 0x80000000ul) asm volatile("dsb sy; nop");
//			addr[0] = 0x80000000ul | (uint8_t)chr;
		#endif
		
	#endif

	if (chr == '\n')
		usartTx('\r');
	usartTx(chr);
}

void dz11charPut(uint_fast8_t line, uint_fast8_t chr)
{
	if (line == 0) {

		//prPutchar(chr);
		if (chr == '\n')
			usartTx('\r');
		usartTx(chr);
	}
}

static bool massStorageAccess(uint8_t op, uint32_t sector, void *buf)
{
	uint_fast8_t nRetries;
	
	switch (op) {
		case MASS_STORE_OP_GET_SZ:
			 *(uint32_t*)buf = sdGetNumSecs();
			 return true;
		
		case MASS_STORE_OP_READ:
			return sdSecRead(sector, buf);
			
		case MASS_STORE_OP_WRITE:
			return sdSecWrite(sector, buf);
	}
	return false;
}

static bool accessRam(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{	
	if (write)
		spiRamWrite(pa, buf, size);
	else
		spiRamRead(pa, buf, size);

	return true;
}

void cycleReport(uint32_t instr, uint32_t pc)
{
	pr("instr [0x%08x] = 0x%08x\n", pc, instr);
}

void reportInvalid(uint32_t addr, uint32_t val, struct CPU* cpu)
{
	uint32_t i, j, k, pa = 0;

	pr("invalid instr seen [0x%08x] = 0x%08x\n", addr, val);

	if (addr < 0x80000000)
		return;
	
	for (i = 0; i < NUM_TLB_BUCKETS; i++) {
		pr(" TLB bucket %u:\n", i);
		j = cpu->hashBuckets[i];
		while (j != 0xff) {

			pr("  item {0x%08x -> 0x%08x asid %u}\n", cpu->tlb[j].va, cpu->tlb[j].pa, cpu->tlb[j].asid);
			j = cpu->tlb[j].nextIdx;

			if (cpu->tlb[j].va / 4096 == addr / 4096)
				pa = cpu->tlb[j].pa / 4096 * 4096 + addr % 4096;
		}
	}
	pr(" instr pa 0x%08x\n", pa);

	for (i = 0; i < ICACHE_NUM_SETS; i++) {
		for (j = 0; j < ICACHE_NUM_WAYS; j++) {

			pr("ic set %2u way %2u @ 0x%08x: ", i, j, cpu->ic[i * ICACHE_NUM_WAYS + j].addr * ICACHE_LINE_SIZE);
			for (k = 0; k < ICACHE_LINE_SIZE; k++)
				pr(" %02x", cpu->ic[i * ICACHE_NUM_WAYS + j].icache[k]);
			pr("\n");
		}
	}
}

bool cpuExtHypercall(void)	//call type in $at, params in $a0..$a3, return in $v0, if any
{
	uint32_t hyperNum = cpuGetRegExternal(MIPS_REG_AT), t,  ramMapNumBits, ramMapEachBitSz;
	uint_fast16_t ofst;
	uint32_t blk, pa;
	uint8_t chr;
	bool ret;

	switch (hyperNum) {
		
		case H_CONSOLE_WRITE:
			chr = cpuGetRegExternal(MIPS_REG_A0);
			if (chr == '\n') {
				prPutchar('\r');
		//		usartTx('\r');
			}
			prPutchar(chr);
		//	usartTx(chr);
			break;
		
		case H_STOR_GET_SZ:
			if (!massStorageAccess(MASS_STORE_OP_GET_SZ, 0, &t))
				return false;
			cpuSetRegExternal(MIPS_REG_V0, t);
			break;
		
		case H_STOR_READ:
			blk = cpuGetRegExternal(MIPS_REG_A0);
			pa = cpuGetRegExternal(MIPS_REG_A1);
			ret = massStorageAccess(MASS_STORE_OP_READ, blk, mDiskBuf);
			for (ofst = 0; ofst < SD_BLOCK_SIZE; ofst += OPTIMAL_RAM_WR_SZ) {

				//pr("write %u bytes to ofst 0x%08x\n", OPTIMAL_RAM_WR_SZ, pa + ofst);
				spiRamWrite(pa + ofst, mDiskBuf + ofst, OPTIMAL_RAM_WR_SZ);
			}

			if(0){
				uint32_t i;

				for (i = 0; i < 512; i++) {
					if (i % 16 == 0)
						pr("%03x ", i);
					pr(" %02x", mDiskBuf[i]);
					if (i % 16 == 15)
						pr("\n");
				}
			}

			cpuSetRegExternal(MIPS_REG_V0, ret);
			if (!ret) {
				
				pr(" rd_block(%u, 0x%08x) -> %d\n", blk, pa, ret);
				sdReportLastError();
				hwError(6);
				while(1);
			}
			break;
		
		case H_STOR_WRITE:
			blk = cpuGetRegExternal(MIPS_REG_A0);
			pa = cpuGetRegExternal(MIPS_REG_A1);
			for (ofst = 0; ofst < SD_BLOCK_SIZE; ofst += OPTIMAL_RAM_RD_SZ)
				spiRamRead(pa + ofst, mDiskBuf + ofst, OPTIMAL_RAM_RD_SZ);
			ret = massStorageAccess(MASS_STORE_OP_WRITE, blk, mDiskBuf);
			cpuSetRegExternal(MIPS_REG_V0, ret);
			if (!ret) {
				
				pr(" wr_block(%u, 0x%08x) -> %d\n", blk, pa, ret);
				sdReportLastError();
				hwError(5);
				while(1);
			}
			break;

		default:
			pr("hypercall %u @ 0x%08x\n", hyperNum, cpuGetRegExternal(MIPS_EXT_REG_PC));
			return false;
	}
	return true;
}

void usartExtRx(uint8_t val)
{
	dz11charRx(0, val);
}

uint32_t getTicksPerSecond(void)
{
	return mCurrentTickRate;
}

void __attribute__((noreturn, used)) micromain(uint32_t desiredHz)
{
	uint8_t eachChipSz, numChips, chipWidth;
	
	//we have a bootlaoder so we do not need SWD-access delay
	mCurrentTickRate = desiredHz / 4000000 * 4000000;

	initHwSuperEarly();
	timebaseInit();
	initHw();
	asm volatile("cpsie i");
	usartInit();
	usartSecondaryInit();
	
	pr("uMIPS.8PL running at 0x%08x\n", &micromain);

	if (!sdCardInit(mDiskBuf)) {
		
		hwError(2);
		pr("SD card init fail\n");
	}
	else if (!spiRamInit(&eachChipSz, &numChips, &chipWidth)) {
		
		hwError(3);
		pr("SPI ram init issue\n");
	}
	else {

		if (!memRegionAdd(RAM_BASE, 128 << 20, accessRam))
			pr("failed to init %s\n", "RAM");
		else if (!decBusInit())
			pr("failed to init %s\n", "DEC BUS");
		else if (!dz11init())
			pr("failed to init %s\n", "DZ11");
		else if (!ds1287init())
			pr("failed to init %s\n", "DS1287");
		else {
						
			pr(
				"uMIPS (BL ver %u, EMU ver %u)\r\n"
				" will run with CPU at %uMHz (requested was %u MHz)\r\n"
				" will run with %ux%uMB x%u RAMS\r\n",
				*(volatile uint8_t*)(FLASH_BASE + 8),
				*(volatile uint8_t*)(SCB->VTOR + 0x10),
				TICKS_PER_SECOND / 1000000, desiredHz / 1000000, 
				numChips, eachChipSz, chipWidth);

			cpuInit(spiRamGetAmt());
			cpuSetRegExternal(MIPS_REG_AT, H_STOR_READ);	//hyper number
			cpuSetRegExternal(MIPS_REG_A0, 0);				//block number
			cpuSetRegExternal(MIPS_REG_A1, 0x00000000);		//PA to write to
			cpuExtHypercall();
			cpuSetRegExternal(MIPS_EXT_REG_PC, 0x80000000);	//jump there
			
			while(1)
				cpuCycle();
		}
	}

	while(1);
}


void __attribute__((used)) report_hard_fault(uint32_t* regs, uint32_t ret_lr, uint32_t *user_sp)
{
	#ifdef SUPPORT_DEBUG_PRINTF
		uint32_t *push = (ret_lr == 0xFFFFFFFD) ? user_sp : (regs + 8), *sp = push + 8;
		unsigned i;
		
		prRaw("============ HARD FAULT ============\n");
		prRaw("R0  = 0x%08X    R8  = 0x%08X\n", (unsigned)push[0], (unsigned)regs[0]);
		prRaw("R1  = 0x%08X    R9  = 0x%08X\n", (unsigned)push[1], (unsigned)regs[1]);
		prRaw("R2  = 0x%08X    R10 = 0x%08X\n", (unsigned)push[2], (unsigned)regs[2]);
		prRaw("R3  = 0x%08X    R11 = 0x%08X\n", (unsigned)push[3], (unsigned)regs[3]);
		prRaw("R4  = 0x%08X    R12 = 0x%08X\n", (unsigned)regs[4], (unsigned)push[4]);
		prRaw("R5  = 0x%08X    SP  = 0x%08X\n", (unsigned)regs[5], (unsigned)sp);
		prRaw("R6  = 0x%08X    LR  = 0x%08X\n", (unsigned)regs[6], (unsigned)push[5]);
		prRaw("R7  = 0x%08X    PC  = 0x%08X\n", (unsigned)regs[7], (unsigned)push[6]);
		prRaw("RA  = 0x%08X    SR  = 0x%08X\n", (unsigned)ret_lr,  (unsigned)push[7]);
		prRaw("SHCSR = 0x%08X\n", SCB->SHCSR);
		#if defined(CPU_TYPE_CM7) || defined(CPU_TYPE_CM33)
	    	prRaw("CFSR  = 0x%08X    HFSR  = 0x%08X\n", SCB->CFSR, SCB->HFSR);
	    	prRaw("MMFAR = 0x%08X    BFAR  = 0x%08X\n", SCB->MMFAR, SCB->BFAR);
		#endif
	    
		prRaw("WORDS @ SP: \n");
		
		for (i = 0; i < 128; i++)
			prRaw("[sp, #0x%03x = 0x%08x] = 0x%08x\n", i * 4, (unsigned)&sp[i], (unsigned)sp[i]);
		
		prRaw("\n\n");
	#endif
	
	while(1);
}

void __attribute__((noreturn, naked, noinline)) HardFault_Handler(void)
{
	asm volatile(
			"push {r4-r7}				\n\t"
			"mov  r0, r8				\n\t"
			"mov  r1, r9				\n\t"
			"mov  r2, r10				\n\t"
			"mov  r3, r11				\n\t"
			"push {r0-r3}				\n\t"
			"mov  r0, sp				\n\t"
			"mov  r1, lr				\n\t"
			"mrs  r2, PSP				\n\t"
			"bl   report_hard_fault		\n\t"
			:::"memory");
}
