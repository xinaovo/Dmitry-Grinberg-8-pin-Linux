/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _TIMEBASE_H_
#define _TIMEBASE_H_

#include <stdint.h>


void timebaseInit(void);
void timebaseDeinit(void);
uint64_t getTime(void);

#ifdef VARIABLE_TICK_RATE
	uint32_t getTicksPerSecond(void);
	#define TICKS_PER_SECOND		(getTicksPerSecond())
#endif


#endif
