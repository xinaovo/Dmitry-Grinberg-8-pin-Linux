/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/


//provided externally
extern void __stack_top();
extern void HardFault_Handler(void);
extern void SysTick_Handler(void);



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

void __attribute__((used, noreturn, naked)) ResetISR(void)
{
	asm volatile(
		"	cpsid	i					\n\t"
		"	adr		r0, 9f				\n\t"
		"	ldmia	r0!, {r1-r5}		\n\t"
		"	movs    r6, #0				\n\t"
		
		"	b		2f					\n\t"
		"1:								\n\t"
		"	ldmia	r2!, {r7}			\n\t"
		"	stmia	r1!, {r7}			\n\t"
		"2:								\n\t"
		"	cmp		r1, r3				\n\t"
		"	bne		1b					\n\t"
		
		"	b		2f					\n\t"
		"1:								\n\t"
		"	stmia	r4!, {r6}			\n\t"
		"2:								\n\t"
		"	cmp		r4, r5				\n\t"
		"	bne		1b					\n\t"
		
		"	cpsie	i					\n\t"
		"	bl		micromain			\n\t"
		".balign 4						\n\t"
		"9:								\n\t"
		"	.word __data_start			\n\t"		//r1
		"	.word __data_data			\n\t"		//r2
		"	.word __data_end			\n\t"		//r3
		"	.word __bss_start			\n\t"		//r4
		"	.word __bss_end				\n\t"		//r5
	);
}


__attribute__ ((section(".vectors"))) void (*const __VECTORS[]) (void) =
{
	&__stack_top,
	ResetISR,
	(void*)0x12,				//bytes: version 0x12, rfu [3]
	HardFault_Handler,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	HardFault_Handler,	// SVCall handler
	0,					// Reserved
	0,					// Reserved
	HardFault_Handler,	// The PendSV handler
	SysTick_Handler,	// The SysTick handler
};



