/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#if defined(CPU_STM32G0)
	#include "stm32g031xx.h"
#elif defined(CPU_SAMD)
	#include "samda1e16b.h"
#elif defined(CPU_RP2350)
	#include "RP2350.h"
#elif defined(CPU_CORTEXEMU)
	#include "CortexEmuCpu.h"
#endif


#include "timebase.h"
#include "printf.h"
#include "ucHw.h"

#define SYSTICK_BITS		24

static uint32_t mTicks = 0;

#ifdef STACKGUARD
	static uint32_t __attribute__((section(".stackguard"))) mStackGuard;
	#define GUARD_XOR 		3141592653
	
	static uint32_t timebasePrvCalcGuardVal(uint32_t tickVal)
	{
		return tickVal ^ GUARD_XOR;
	}
	
	static void timebasePrvSetGuardVal(uint32_t tickVal)
	{
		mStackGuard = timebasePrvCalcGuardVal(tickVal);
	}
	
	static void timebasePrvCheckGuardVal(uint32_t tickVal)
	{
		if (mStackGuard != timebasePrvCalcGuardVal(tickVal)) {
			pr("stack guard violation!\n");
			hwError(10);
			while(1);
		}
	}
#else

	static void timebasePrvSetGuardVal(uint32_t tickVal)
	{
		//empty
	}
	
	static void timebasePrvCheckGuardVal(uint32_t tickVal)
	{
		//empty
	}

#endif

void timebaseDeinit(void)
{
	SysTick->CTRL = 0;
}

void timebaseInit(void)
{
	SysTick->CTRL = 0;
	
	timebasePrvSetGuardVal(mTicks);
	NVIC_SetPriority(SysTick_IRQn, 1);
	
	//setup SysTick
	SysTick->CTRL = 0;
	SysTick->LOAD = ((1 << SYSTICK_BITS) - 1);
	SysTick->VAL = 0;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

void SysTick_Handler(void)
{
	timebasePrvCheckGuardVal(mTicks);
	mTicks++;
	timebasePrvSetGuardVal(mTicks);
}

uint64_t getTime(void)
{
	uint32_t hi, lo;
	
#ifdef CPU_CM7

	uint32_t hi2;

	do {		//this construction brought to you by Cortex-M7
		hi = mTicks;
		asm volatile("":::"memory");
		hi2 = mTicks;
		asm volatile("":::"memory");
		lo = SysTick->VAL;
		asm volatile("":::"memory");
	} while (hi != hi2 || hi1 != mTicks);
	
#else

	do {
		hi = mTicks;
		asm volatile("":::"memory");
		lo = SysTick->VAL;
		asm volatile("":::"memory");
	} while (hi != mTicks);

#endif

	return (((uint64_t)hi) << SYSTICK_BITS) + (((1 << SYSTICK_BITS) - 1) - lo);
}