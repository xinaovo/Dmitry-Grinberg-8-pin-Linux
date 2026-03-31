/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _USART_H_
#define _USART_H_

#include <stdint.h>


void usartInit(void);
void usartSetBuadrate(uint32_t baud);
void usartTx(uint8_t ch);

void usartTxEx(uint8_t channel, uint8_t ch);



//only sometimes
#ifdef USART_NEEDS_SECONDARY_INIT
	void usartSecondaryInit(void);
#else
	static inline void usartSecondaryInit(void) {}
#endif

//externally provided
void usartExtRx(uint8_t val);



#endif
