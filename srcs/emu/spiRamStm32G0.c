/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stm32g031xx.h>
#include <string.h>
#include <stddef.h>
#include "printf.h"
#include "spiRam.h"
#include "cpu.h"


#define PORT_NCS			GPIOB
#define PIN_NCS				8

#define PORT_CLOCK			GPIOA
#define PIN_CLK				1		//in porta

#define SPI_RAM				SPI1

//allocating 16MB of address space per chip is simpler but wil not allow booting with 2-3 2MB chips. this can be fixed, but i think i'll live...

static void spiRamCacheInit(void);


static inline void __attribute__((always_inline)) spiRamPrvSelect(void)
{
	PORT_NCS->BRR = 1 << PIN_NCS;
}

static inline void __attribute__((always_inline)) spiRamPrvDeselect(void)
{
	while (SPI_RAM->SR & SPI_SR_BSY);
	while (!(PORT_CLOCK->IDR & (1 << (PIN_CLK))));	//not busy doesnt mean clock is idle.. wait for that
	PORT_NCS->BSRR = 1 << PIN_NCS;
}

			#include "timebase.h"
			static void spiRamReadLL_32(uint32_t addr, void *dataP, uint_fast16_t sz);
			static void spiRamWriteLL(uint32_t addr, const void *dataP, uint_fast16_t sz);

			void test(void)
			{
				uint8_t vals1[32], vals2[sizeof(vals1)];
				uint32_t i, rdSpeed, wrSpeed;
				uint64_t time;


				time = getTime();
				spiRamReadLL_32(0x123456, vals1, sizeof(vals1));
				rdSpeed = getTime() - time;

				for (i = 0; i < sizeof(vals1); i++)
					vals2[i] = vals1[i] ^ 0xff;
				time = getTime();
				spiRamWriteLL(0x123456, vals2, sizeof(vals2));
				wrSpeed = getTime() - time;
				spiRamReadLL_32(0x123456, vals2, sizeof(vals2));

				for (i = 0; i < 32; i++) {
					pr ("%2u %02xh->%02xh-%02xh\n", i, vals1[i], 0xff ^ vals1[i], vals2[i]);
				}
				pr("read took %u ticks, write took %u\n", rdSpeed, wrSpeed);
			}

#define RXDMACFG	(DMA_CCR_PL_1 | DMA_CCR_PL_0)
#define TXDMACFG	(DMA_CCR_DIR_Msk | DMA_CCR_PL_1 | DMA_CCR_PL_0 | DMA_CCR_MINC)
#define SPICR2		(7 << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH

bool spiRamInit(uint8_t *eachChipSzP, uint8_t *numChipsP, uint8_t *chipWidthP)
{
	uint_fast8_t i, j, k;
	uint8_t chipid[8];
	
	SPI_RAM->CR2 = SPICR2;
	SPI_RAM->CR1 = SPI_CR1_MSTR | SPI_CR1_SPE | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPOL | SPI_CR1_CPHA;
	
	//clear errors
	(void)SPI_RAM->DR;
	(void)SPI_RAM->DR;
	(void)SPI_RAM->DR;
	(void)SPI_RAM->DR;
	(void)SPI_RAM->SR;	//reset OVR flag

	//verify chip seen
	
	pr("Checking RAM\n");
	
	spiRamPrvSelect();
	
	*(volatile uint8_t*)&SPI_RAM->DR = 0x9f;	//read ID
	*(volatile uint8_t*)&SPI_RAM->DR = 0x00;	//addr
	*(volatile uint8_t*)&SPI_RAM->DR = 0x00;
	*(volatile uint8_t*)&SPI_RAM->DR = 0x00;
	while ((SPI_RAM->SR & SPI_SR_FRLVL) != SPI_SR_FRLVL);	//wait for 4 bytes RXed
	for (j = 0; j < 4; j++)	//we get two bytes at a time!
		(void)*(volatile uint8_t*)&SPI_RAM->DR;
	
	for (k = 0; k < 2; k++) {
		
		for (j = 0; j < 4; j++)
			*(volatile uint8_t*)&SPI_RAM->DR = 0x00;
		while ((SPI_RAM->SR & SPI_SR_FRLVL) != SPI_SR_FRLVL);	//wait for 4 bytes RXed
		for (j = 0; j < 4; j++)
			chipid[k * 4 + j] = *(volatile uint8_t*)&SPI_RAM->DR;
	}
	
	spiRamPrvDeselect();
	
	pr(" chip id: %02x %02x %02x %02x %02x %02x %02x %02x\n", 
		chipid[0], chipid[1], chipid[2], chipid[3],
		chipid[4], chipid[5], chipid[6], chipid[7]);
	
	j = 0;
	k = 0xff;
	for (i = 0; i < sizeof(chipid); i++) {

		j |= chipid[i];
		k &= chipid[i];
	}

	//configure DMA1 ch1 for RX
	DMA1_Channel1->CPAR = (uintptr_t)&SPI_RAM->DR;
	DMA1_Channel1->CCR = RXDMACFG;
	DMAMUX1_Channel0->CCR = 16;	//spi1rx

	DMA1_Channel2->CPAR = (uintptr_t)&SPI_RAM->DR;
	DMA1_Channel2->CCR = TXDMACFG;
	DMAMUX1_Channel1->CCR = 17;	//spi1tx

	if (!j)
		pr("  no chip here (0x00)\n");
	else if (k == 0xff)
		pr("  no chip here (0xff)\n");
	else {
	
		spiRamCacheInit();
		
		*eachChipSzP = 8;
		*numChipsP = 1;
		*chipWidthP = 1;

		return true;
	}

	return false;
}

uint32_t spiRamGetAmt(void)
{
	return 8 << 20;
}

static void spiRamReadLL_32_withDummyBytes(uint32_t addr, void *dataP, uint_fast16_t sz)	//assume not called with zero
{
	uint_fast8_t i;
	uint8_t *data = dataP;
	

	PORT_NCS->BRR = 1 << PIN_NCS;

	addr = __builtin_bswap32(addr) + 0x0b;
	*(volatile uint16_t*)&SPI_RAM->DR = addr;
	*(volatile uint16_t*)&SPI_RAM->DR = addr >> 16;
	
	while (!(SPI_RAM->SR & SPI_SR_FRLVL_1));			//wait for two bytes in RX fifo (replies to command and high 8 of address)
	(void)*(volatile uint8_t*)&SPI_RAM->DR;			//read them
	(void)*(volatile uint8_t*)&SPI_RAM->DR;			//read them
	
	*(volatile uint16_t*)&SPI_RAM->DR = 0;			//send dummy byte and first zero byte

	while (!(SPI_RAM->SR & SPI_SR_FRLVL_1));			//wait for two bytes in RX fifo (replies to low 16 of address)
	(void)*(volatile uint8_t*)&SPI_RAM->DR;			//read them
	(void)*(volatile uint8_t*)&SPI_RAM->DR;			//read them

	*(volatile uint16_t*)&SPI_RAM->DR = 0;			//send next two zeroes

	while (!(SPI_RAM->SR & SPI_SR_RXNE));			//wait for one byte in RX fifo (reply to bummy byte)
	(void)*(volatile uint8_t*)&SPI_RAM->DR;			//read it and send a zero
	*(volatile uint8_t*)&SPI_RAM->DR = 0;			//send fourth zero
	
	DMA1_Channel1->CMAR = (uintptr_t)dataP;
	DMA1_Channel1->CNDTR = sz;
	DMA1_Channel1->CCR = RXDMACFG | DMA_CCR_MINC | DMA_CCR_EN_Msk;
	SPI_RAM->CR2 = SPICR2 | SPI_CR2_RXDMAEN_Msk;

	for (i = 0; i < sz / sizeof(uint16_t) - 2; i++) {
	
		while (SPI_RAM->SR & SPI_SR_FTLVL_1);				//wait for two spaces in TX fifo (should be there already)
		*(volatile uint16_t*)&SPI_RAM->DR = 0;				//send zeroes to RX bytes
	}
	while (DMA1_Channel1->CNDTR);
	while (SPI_RAM->SR & SPI_SR_BSY);
	while (!(PORT_CLOCK->IDR & (1 << (PIN_CLK))));	//not busy doesnt mean clock is idle.. wait for that
	PORT_NCS->BSRR = 1 << PIN_NCS;

	asm volatile("":::"memory");
	SPI_RAM->CR2 = SPICR2;
	DMA1_Channel1->CCR = 0;
}

static void spiRamReadLL_32_noDummyBytes(uint32_t addr, void *dataP, uint_fast16_t sz)	//assume not called with zero
{
	uint_fast8_t i;
	uint8_t *data = dataP;
	

	PORT_NCS->BRR = 1 << PIN_NCS;

	addr = __builtin_bswap32(addr) + 0x03;
	*(volatile uint16_t*)&SPI_RAM->DR = addr;
	*(volatile uint16_t*)&SPI_RAM->DR = addr >> 16;
	
	while (!(SPI_RAM->SR & SPI_SR_FRLVL_1));			//wait for two bytes in RX fifo (replies to command and high 8 of address)
	(void)*(volatile uint8_t*)&SPI_RAM->DR;			//read them
	(void)*(volatile uint8_t*)&SPI_RAM->DR;			//read them
	
	*(volatile uint16_t*)&SPI_RAM->DR = 0;			//send two zeroes

	while (!(SPI_RAM->SR & SPI_SR_FRLVL_1));			//wait for two bytes in RX fifo (replies to low 16 of address)
	(void)*(volatile uint8_t*)&SPI_RAM->DR;			//read them
	(void)*(volatile uint8_t*)&SPI_RAM->DR;			//read them

	*(volatile uint16_t*)&SPI_RAM->DR = 0;			//send next two zeroes
	
	DMA1_Channel1->CMAR = (uintptr_t)dataP;
	DMA1_Channel1->CNDTR = sz;
	DMA1_Channel1->CCR = RXDMACFG | DMA_CCR_MINC | DMA_CCR_EN_Msk;
	SPI_RAM->CR2 = SPICR2 | SPI_CR2_RXDMAEN_Msk;

	for (i = 0; i < sz / sizeof(uint16_t) - 2; i++) {
	
		while (SPI_RAM->SR & SPI_SR_FTLVL_1);				//wait for two spaces in TX fifo (should be there already)
		*(volatile uint16_t*)&SPI_RAM->DR = 0;				//send zeroes to RX bytes
	}
	while (DMA1_Channel1->CNDTR);
	while (SPI_RAM->SR & SPI_SR_BSY);
	while (!(PORT_CLOCK->IDR & (1 << (PIN_CLK))));	//not busy doesnt mean clock is idle.. wait for that
	PORT_NCS->BSRR = 1 << PIN_NCS;

	asm volatile("":::"memory");
	SPI_RAM->CR2 = SPICR2;
	DMA1_Channel1->CCR = 0;
}

static void spiRamReadLL_32(uint32_t addr, void *dataP, uint_fast16_t sz)	//assume not called with zero
{
	return spiRamReadLL_32_withDummyBytes(addr, dataP, sz);
}

static void spiRamWriteLL(uint32_t addr, const void *dataP, uint_fast16_t sz)
{
	const uint8_t *data = (const uint8_t*)dataP;
	static volatile uint8_t dummy;
	
	DMA1_Channel2->CMAR = (uintptr_t)dataP;
	DMA1_Channel2->CNDTR = sz;
	DMA1_Channel2->CCR = TXDMACFG | DMA_CCR_EN_Msk;
	
	DMA1_Channel1->CMAR = (uintptr_t)&dummy;	//read there, no MINC
	DMA1_Channel1->CNDTR = sz + 4;
	DMA1_Channel1->CCR = RXDMACFG | DMA_CCR_EN_Msk;
	
	PORT_NCS->BRR = 1 << PIN_NCS;
	
	addr = __builtin_bswap32(addr) + 0x02;
	*(volatile uint16_t*)&SPI_RAM->DR = addr;
	*(volatile uint16_t*)&SPI_RAM->DR = addr >> 16;

	SPI_RAM->CR2 = SPICR2 | SPI_CR2_RXDMAEN_Msk | SPI_CR2_TXDMAEN_Msk;
	while (DMA1_Channel1->CNDTR);
	while (SPI_RAM->SR & SPI_SR_BSY);
	while (!(PORT_CLOCK->IDR & (1 << (PIN_CLK))));	//not busy doesnt mean clock is idle.. wait for that
	
	SPI_RAM->CR2 = SPICR2;
	DMA1_Channel1->CCR = 0;
	DMA1_Channel2->CCR = 0;

	PORT_NCS->BSRR = 1 << PIN_NCS;
}

#define CACHE_LINE_SIZE		32		//must be at least the size of icache line, or else...
#define CACHE_NUM_WAYS		2		//number of lines a given PA can be in
#define CACHE_NUM_SETS		32		//number of buckets of PAs



struct CacheLine {
	uint32_t	addr	: 31;
	uint32_t	dirty	:  1;
	union {
		uint32_t dataW[CACHE_LINE_SIZE / sizeof(uint32_t)];
		uint16_t dataH[CACHE_LINE_SIZE / sizeof(uint16_t)];
		uint8_t dataB[CACHE_LINE_SIZE / sizeof(uint8_t)];
	};
};

struct CacheSet {
	struct CacheLine line[CACHE_NUM_WAYS];
};

struct Cache {
	uint32_t random;
	struct CacheSet set[CACHE_NUM_SETS];
};

static struct Cache mCache;

static void spiRamCacheInit(void)
{
	uint_fast16_t way, set;
	
	for (set = 0; set < CACHE_NUM_SETS; set++) {
		for (way = 0; way < CACHE_NUM_WAYS; way++) {
			
			mCache.set[set].line[way].addr = -1;	//definitely invalid
		}
	}
}

static uint_fast16_t spiRamCachePrvHash(uint32_t addr)
{
	addr /= CACHE_LINE_SIZE;
	addr %= CACHE_NUM_SETS;
	
	return addr;
}

static uint_fast16_t spiRamCachePrvPickVictim(struct Cache *cache)
{
	uint32_t t = cache->random;
	
	t *= 214013;
	t += 2531011;
	cache->random = t;
	
	t >>= 24;
	
	t %= CACHE_NUM_WAYS;
	
	return t;
}

static struct CacheLine* spiRamCachePrvFillLine(struct Cache *cache, struct CacheSet *set, uint32_t addr, bool loadFromRam)
{
	uint_fast16_t idx = spiRamCachePrvPickVictim(cache);
	struct CacheLine *line = &set->line[idx];
	
	
//	pr("picked victim way %u currently holding addr 0x%08x, %s\n", idx, line->addr * CACHE_LINE_SIZE, line->dirty ? "DIRTY" : "CLEAN");
	if (line->dirty) {	//clean line
		

//		pr(" flushing -> %08x\n", line->addr * CACHE_LINE_SIZE);
		spiRamWriteLL(line->addr * CACHE_LINE_SIZE, line->dataW, CACHE_LINE_SIZE);
	}
	
	if (loadFromRam) {
//		uint32_t *dst = line->dataW;
		

		spiRamReadLL_32(addr / CACHE_LINE_SIZE * CACHE_LINE_SIZE, line->dataW, CACHE_LINE_SIZE);
		
//		pr(" filled %08x -> %08x %08x %08x %08x %08x %08x %08x %08x\n", addr, 
//				dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7]);
	}
	line->dirty = 0;
	line->addr = addr / CACHE_LINE_SIZE;
	
	return line;
}

void spiRamRead(uint32_t addr, void *dataP, uint_fast16_t sz)	//assume not called with zero
{
	struct Cache *cache = &mCache;
	struct CacheSet *set = &cache->set[spiRamCachePrvHash(addr)];
	uint32_t *dptr, *dst = (uint32_t*)dataP;
	struct CacheLine *line; 
	uint_fast16_t i;

	//pr("read %u @ 0x%08x -> set %u\n", sz, addr, set - &cache->set[0]);

	for (i = 0, line = &set->line[0]; i < CACHE_NUM_WAYS; i++, line++) {
		
		if (line->addr == addr / CACHE_LINE_SIZE) {

			goto found;
		}
	}
	
	//not found
	
	if (sz == CACHE_LINE_SIZE) {
		
		//no point allocating it
		spiRamReadLL_32(addr, dataP, CACHE_LINE_SIZE);
		//pr("read %u @ 0x%08x idx %u -> %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
		//		dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7]);

		return;
	}
	
	//pr("fill\n", sz, addr);
	line = spiRamCachePrvFillLine(cache, set, addr, true);

found:
	
	switch (sz) {
		case 1:
			*(uint8_t*)dataP = line->dataB[(addr % CACHE_LINE_SIZE) / sizeof(uint8_t)];
		//	pr("read %u @ 0x%08x idx %u -> %02x\n", sz, addr, line - &set->line[0], *(uint8_t*)dataP);
			break;
		
		case 2:
			*(uint16_t*)dataP = line->dataH[(addr % CACHE_LINE_SIZE) / sizeof(uint16_t)];
		//	pr("read %u @ 0x%08x idx %u -> %04x\n", sz, addr, line - &set->line[0], *(uint16_t*)dataP);
			break;
		
		case 4:
			*(uint32_t*)dataP = line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
		//	pr("read %u @ 0x%08x idx %u -> %08x\n", sz, addr, line - &set->line[0], *(uint32_t*)dataP);
			break;
		
		case 32:	//icache fill or SD access
		
			dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / 32];
			dst[0] = dptr[0];
			dst[1] = dptr[1];
			dst[2] = dptr[2];
			dst[3] = dptr[3];
			dst[4] = dptr[4];
			dst[5] = dptr[5];
			dst[6] = dptr[6];
			dst[7] = dptr[7];
		//	pr("read %u @ 0x%08x idx %u -> %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
		//		dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7]);
			break;
		
		default:
			pr("unknown size %u\n", sz);
			while(1);
	}
}

void spiRamWrite(uint32_t addr, const void *dataP, uint_fast16_t sz)
{
	struct Cache *cache = &mCache;
	struct CacheSet *set = &cache->set[spiRamCachePrvHash(addr)];
	const uint32_t *src = (const uint32_t*)dataP;
	struct CacheLine *line; 
	uint32_t *dptr;
	uint_fast16_t i;
	
	
	//pr("write %u @ 0x%08x -> set %u\n", sz, addr, set - cache->set);

	for (i = 0, line = &set->line[0]; i < CACHE_NUM_WAYS; i++, line++) {
		
		if (line->addr == addr / CACHE_LINE_SIZE) {
			goto found;
		}
	}
	
	//do not allocate full-line writes
	if (sz == CACHE_LINE_SIZE) {
		
		//no point allocating it
		spiRamWriteLL(addr, dataP, CACHE_LINE_SIZE);

	//	pr("write %u @ 0x%08x idx %u -> %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//			src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7]);

		return;
	}

	//not found
	//pr("fill\n", sz, addr);
	line = spiRamCachePrvFillLine(cache, set, addr, sz != 32);

found:
	
	switch (sz) {
		case 1:
		//	pr("write %u @ 0x%08x idx %u <- %02x\n", sz, addr, line - &set->line[0], *(uint8_t*)dataP);
			line->dataB[(addr % CACHE_LINE_SIZE) / sizeof(uint8_t)] = *(uint8_t*)dataP;
			break;
		
		case 2:
		//	pr("write %u @ 0x%08x idx %u <- %04x\n", sz, addr, line - &set->line[0], *(uint16_t*)dataP);
			line->dataH[(addr % CACHE_LINE_SIZE) / sizeof(uint16_t)] = *(uint16_t*)dataP;
			break;
		
		case 4:
		//	pr("write %u @ 0x%08x idx %u <- %08x\n", sz, addr, line - &set->line[0], *(uint32_t*)dataP);
			line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)] = *(uint32_t*)dataP;
			break;
		
		case 32:	//icache fill or SD access
		//	pr("write %u @ 0x%08x idx %u <- %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
		//		src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7]);
			dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / 32];
			dptr[0] = src[0];
			dptr[1] = src[1];
			dptr[2] = src[2];
			dptr[3] = src[3];
			dptr[4] = src[4];
			dptr[5] = src[5];
			dptr[6] = src[6];
			dptr[7] = src[7];
			break;
		
		default:
			pr("unknown size %u\n", sz);
			while(1);
	}
	line->dirty = 1;
}

