#include "stm32g031xx.h"
#include "timebase.h"
#include <stddef.h>
#include "usart.h"


#define BAUDRATE		115200
#define PORT			GPIOA
#define PIN				13

extern uint32_t mUartDivisor;

static void __attribute__((naked, noinline, section(".ramcode"))) usartTxPrvAsm(uint8_t byte)
{
	asm volatile(
		".syntax unified				\n\t"
		"	push	{r4, lr}			\n\t"
		"	movs	r1, #0x7			\n\t"
		"	lsls	r1, #9				\n\t"
		"	adds	r1, r1, r0			\n\t"
		"	adds	r0, r1, r0			\n\t"
		"	movs	r1, #11				\n\t"
		"	ldr		r2, =%0				\n\t"
		"	movs	r3, #1				\n\t"
		"	lsls	r3, r3, %1			\n\t"
		"	cpsid	i					\n\t"
		"8:								\n\t"
		"	ldr		r4, 3f				\n\t"
		"7:								\n\t"
		"	subs	r4, #1				\n\t"
		"	bne		7b					\n\t"
		"	lsrs	r0, r0, #1			\n\t"
		"	bcc		1f					\n\t"
		"	nop							\n\t"
		"	str		r3, [r2, %2]		\n\t"
		"	subs	r1, #1				\n\t"
		"	bne		8b					\n\t"
		"	b		9f					\n\t"
		"1:								\n\t"
		"	str		r3, [r2, %3]		\n\t"
		"	subs	r1, #1				\n\t"
		"	bne		8b					\n\t"
		"9:								\n\t"
		"	cpsie	i					\n\t"
		"	pop		{r4, pc}			\n\t"
		".balign 4						\n\t"
		"3:								\n\t"
		".globl mUartDivisor			\n\t"
		"mUartDivisor:					\n\t"
		"	.word 0x5					\n\t"

		:
		:"i"(PORT), "i"(PIN), "i"(offsetof(GPIO_TypeDef, BSRR)), "i"(offsetof(GPIO_TypeDef, BRR))
		:"cc"
	);
}

void usartSecondaryInit(void)
{
	const unsigned cyclesPerBit = (TICKS_PER_SECOND + BAUDRATE / 2) / BAUDRATE;
	const unsigned cyclesWeUsePerBit = 2 /* load delay value */ + 2 /* last iteration of the delay loop */ + 8 /* tx code */;
	const unsigned cyclesPerDelayLoop = 3;	//except last which already accounted for
	const unsigned delayCyclesNeeded = (cyclesPerBit - cyclesWeUsePerBit + cyclesPerDelayLoop / 2) / cyclesPerDelayLoop;

	mUartDivisor = delayCyclesNeeded + 1;
}

void __attribute__((naked)) usartTx(uint8_t byte)							//a better veneer than gcc will use
{
	asm volatile(
		"ldr	r3, =%0	\n\t"
		"bx		r3		\n\t"
		:
		:"i"(&usartTxPrvAsm)
		:"memory", "cc", "r0", "r1", "r2", "r3", "r12", "lr"
	);
}