/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include  <stdint.h>
#define VEC_(nm, pfx)	void nm##pfx(void) __attribute__ ((weak, alias ("IntDefaultHandler"))) 
#define VEC(nm)		VEC_(nm, _Handler)
#define VECI(nm)	VEC_(nm, _IRQHandler)


void __attribute__ ((weak)) IntDefaultHandler(void);
VEC(NMI);
VEC(HardFault);
VEC(SVC);
VEC(PendSV);
VEC(SysTick);

VECI(WWDG);
VECI(PVD);
VECI(RTC_TAMP);
VECI(FLASH);
VECI(RCC);
VECI(EXTI0_1);
VECI(EXTI2_3);
VECI(EXTI4_15);
VECI(DMA1_Channel1);
VECI(DMA1_Channel2_3);
VECI(DMA1_Ch4_5_DMAMUX1_OVR);
VECI(ADC1);
VECI(TIM1_BRK_UP_TRG_COM);
VECI(TIM1_CC);
VECI(TIM2);
VECI(TIM3);
VECI(LPTIM1);
VECI(LPTIM2);
VECI(TIM14);
VECI(TIM16);
VECI(TIM17);
VECI(I2C1);
VECI(I2C2);
VECI(SPI1);
VECI(SPI2);
VECI(USART1);
VECI(USART2);
VECI(LPUART1);





//micromain must exist
extern void micromain(uint32_t desiredHz);

//stack top (provided by linker)
extern void __stack_top();
extern uint32_t __data_data[];
extern uint32_t __data_start[];
extern uint32_t __data_end[];
extern uint32_t __bss_start[];
extern uint32_t __bss_end[];




void __attribute__((noreturn)) IntDefaultHandler(void)
{
	while (1) {		
		asm("wfi":::"memory");
	}
}

void __attribute__((noreturn)) ResetISR(uint32_t desiredHz)
{
	volatile uint32_t *dst, *src, *end;

	//copy data
	dst = __data_start;
	src = __data_data;
	end = __data_end;
	while(dst != end)
		*dst++ = *src++;

	//init bss
	dst = __bss_start;
	end = __bss_end;
	while(dst != end)
		*dst++ = 0;

	micromain(desiredHz);

//if main returns => bad
	while(1);
}


__attribute__ ((section(".vectors"))) void (*const __VECTORS[]) (void) =
{
	&__stack_top,
	(void*)ResetISR,
	NMI_Handler,
	HardFault_Handler,
	(void*)0x00000007,			//version read by bootloader (offset 0x10). SIGNED (to allow empty flash to be -1)
	0,
	0,
	0,
	0,
	0,
	0,
	SVC_Handler,		// SVCall handler
	0,					// Reserved
	0,					// Reserved
	PendSV_Handler,		// The PendSV handler
	SysTick_Handler,	// The SysTick handler
	
	// Chip Level - STM32G0
	WWDG_IRQHandler,
	PVD_IRQHandler,
	RTC_TAMP_IRQHandler,
	FLASH_IRQHandler,
	RCC_IRQHandler,
	EXTI0_1_IRQHandler,
	EXTI2_3_IRQHandler,
	EXTI4_15_IRQHandler,
	0,
	DMA1_Channel1_IRQHandler,
	DMA1_Channel2_3_IRQHandler,
	DMA1_Ch4_5_DMAMUX1_OVR_IRQHandler,
	ADC1_IRQHandler,
	TIM1_BRK_UP_TRG_COM_IRQHandler,
	TIM1_CC_IRQHandler,
	TIM2_IRQHandler,
	TIM3_IRQHandler,
	LPTIM1_IRQHandler,
	LPTIM2_IRQHandler,
	TIM14_IRQHandler,
	0,
	TIM16_IRQHandler,
	TIM17_IRQHandler,
	I2C1_IRQHandler,
	I2C2_IRQHandler,
	SPI1_IRQHandler,
	SPI2_IRQHandler,
	USART1_IRQHandler,
	USART2_IRQHandler,
	LPUART1_IRQHandler,
};





unsigned __attribute__((naked)) strlen(const char *a)
{
	asm volatile(
		".syntax unified				\n\t"
		"	mov		r1, r0				\n\t"
		"1:								\n\t"
		"	ldrb	r2, [r0]			\n\t"
		"	cmp		r2, #0				\n\t"
		"	beq		1f					\n\t"
		"	adds	r0, #1				\n\t"
		"	b		1b					\n\t"
		"1:								\n\t"
		"	subs	r0, r1				\n\t"
		"	bx		lr					\n\t"
	);
}

int __attribute__((naked)) memcmp(const void *a, const void *b, unsigned len)
{
	asm volatile(
		".syntax unified				\n\t"
		"	mov		r12, r4				\n\t"
		"	movs	r3, #0				\n\t"
		"	cmp		r2, #0				\n\t"
		"	beq		3f					\n\t"
		"2:								\n\t"
		"	ldrb	r3, [r0]			\n\t"
		"	ldrb	r4, [r1]			\n\t"
		"	adds	r0, #1				\n\t"
		"	adds	r1, #1				\n\t"
		"	subs	r3, r4				\n\t"
		"	bne		3f					\n\t"
		"	subs	r2, #1				\n\t"
		"	bne		2b					\n\t"
		"3:								\n\t"
		"	mov		r0, r3				\n\t"
		"	mov		r4, r12				\n\t"
		"	bx		lr					\n\t"
	);
}

void* __attribute__((naked)) memset(void *dst, int val, unsigned len)
{
	asm volatile(
		".syntax unified				\n\t"
		"	mov		r12, r0				\n\t"
		"	cmp		r2, #0				\n\t"
		"	beq		2f					\n\t"
		"1:								\n\t"
		"	subs	r2, #1				\n\t"
		"	strb	r1, [r0, r2]		\n\t"
		"	bne		1b					\n\t"
		"2:								\n\t"
		"	mov		r0, r12				\n\t"
		"	bx		lr					\n\t"
	);
}

void* __attribute__((naked)) memcpy(void *dst, const void *src, unsigned len)
{
	asm volatile(
		".syntax unified				\n\t"
		"	mov		r12, r0				\n\t"
		"	cmp		r2, #0				\n\t"
		"	beq		2f					\n\t"
		"1:								\n\t"
		"	subs	r2, #1				\n\t"
		"	ldrb	r3, [r1, r2]		\n\t"
		"	strb	r3, [r0, r2]		\n\t"
		"	bne		1b					\n\t"
		"2:								\n\t"
		"	mov		r0, r12				\n\t"
		"	bx		lr					\n\t"
	);
}
