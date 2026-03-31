/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#if defined(CPU_STM32G0)
	#include "stm32g031xx.h"
	#define USART		USART2
	#define IRQH_NAME	USART2_IRQHandler
	#define IRQE_NAME	USART2_IRQn
#elif defined(CPU_CM7)
	#include "stm32h7b0xx.h"
	#define USART		USART2
	#define IRQH_NAME	USART2_IRQHandler
	#define IRQE_NAME	USART2_IRQn
#endif
#include "timebase.h"
#include "printf.h"
#include "usart.h"


void dz11rxSpaceNowAvail(uint_fast8_t line)
{
	(void)line;
}

void usartSetBuadrate(uint32_t baud)
{
	uint32_t cr;
	
	USART->CR1 = (cr = USART->CR1) &~ USART_CR1_UE;
	USART->BRR = (TICKS_PER_SECOND + baud / 2) / baud;
	USART->CR1 = cr;
}

void usartInit(void)
{
	USART->CR2 = USART_CR2_SWAP;
	USART->CR3 = 0;
	usartSetBuadrate(115200);
	USART->CR1 = USART_CR1_RXNEIE_RXFNEIE
	#ifndef UART_RX_ONLY
		 | USART_CR1_TE
	#endif
		 | USART_CR1_RE;
	USART->CR1 |= USART_CR1_UE;

	NVIC_EnableIRQ(IRQE_NAME);
}

void IRQH_NAME(void)
{
	while (USART->ISR & USART_ISR_RXNE_RXFNE)
		usartExtRx(USART->RDR);
	USART->ICR = -1;
}

#ifndef UART_RX_ONLY

	void usartTx(uint8_t ch)
	{
		while (!(USART->ISR & USART_ISR_TXE_TXFNF));
		USART->TDR = ch;
	}

#endif

