/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#if defined(CPU_STM32G0)
	#include "stm32g031xx.h"
#elif defined(CPU_CM7)
	#include "stm32h7b0xx.h"
#endif

#include <stdint.h>
#include "timebase.h"
#include "printf.h"
#include "ds1287.h"
#include "mem.h"
#include "soc.h"
#include "cpu.h"

//https://pdfserv.maximintegrated.com/en/ds/DS12885-DS12C887A.pdf


/*
	XXX: alarm handling is wrong. see page 13 of DS, psecifically about "don't care" values for alarm bytes
	see atsamd implementation
*/


#define RTC_CTRLA_UIP		0x80
#define RTC_CTRLA_DV_MASK	0x70
#define RTC_CTRLA_DV_SHIFT	4
#define RTC_CTRLA_RS_MASK	0x0f
#define RTC_CTRLA_RS_SHIFT	0

#define RTC_CTRLB_SET		0x80
#define RTC_CTRLB_PIE		0x40
#define RTC_CTRLB_AIE		0x20
#define RTC_CTRLB_UIE		0x10
#define RTC_CTRLB_SQWE		0x08
#define RTC_CTRLB_DM		0x04
#define RTC_CTRLB_2412		0x02
#define RTC_CTRLB_DSE		0x01

#define RTC_CTRLC_IRQF		0x80
#define RTC_CTRLC_PF		0x40
#define RTC_CTRLC_AF		0x20
#define RTC_CTRLC_UF		0x10

#define RTC_CTRLD_VRT		0x80


struct Config {
	
	//irq flags (bytes to avoid RMW issues
	volatile uint8_t pf;
	volatile uint8_t af;
	volatile uint8_t uf;
	volatile uint8_t irqf;
	
	//irq enables
	uint8_t pie			: 1;
	uint8_t aie			: 1;
	uint8_t uie			: 1;
	
	//misc & config
	uint8_t m2412		: 1;	//1 for 24h mode, 0 for 12h
	uint8_t dse			: 1;
	uint8_t rs			: 4;
	uint8_t dv			: 3;
	uint8_t sqwe		: 1;
	uint8_t dm			: 1;	//1 for binary, 0 for BCD
	
	uint8_t ram[0x72];
} mDS1287;



static uint_fast8_t ds1287prvToBcdIfNeeded(uint_fast8_t val)
{
	if (mDS1287.dm)
		val = (val / 10) * 16 + val % 10;
	
	return val;
}

static uint_fast8_t ds1287prvFromBcdIfNeeded(uint_fast8_t val)
{
	
	if (mDS1287.dm)
		val = (val / 16) * 10 + val % 16;
	
	return val;
}

static void ds1287prvSimpleAccess(volatile uint32_t *reg, uint8_t *buf, bool write, uint32_t mask, uint_fast8_t shift)
{
	if (write)
		*reg = (*reg &~ mask) | ((uint32_t)ds1287prvToBcdIfNeeded(*buf)) << shift;
	else
		*buf = ds1287prvFromBcdIfNeeded((*reg & mask) >> shift);
}

static void ds1287prvSecondsAccess(volatile uint32_t *reg, uint8_t *buf, bool write)
{
	ds1287prvSimpleAccess(reg, buf, write, RTC_TR_ST | RTC_TR_SU, RTC_TR_SU_Pos);
}

static void ds1287prvMinutesAccess(volatile uint32_t *reg, uint8_t *buf, bool write)
{
	ds1287prvSimpleAccess(reg, buf, write, RTC_TR_MNT | RTC_TR_MNU, RTC_TR_MNU_Pos);
}

static void ds1287prvHoursAccess(volatile uint32_t *reg, uint8_t *buf, bool write)
{
	uint_fast8_t t;
	
	if (write) {
		
		t = ds1287prvToBcdIfNeeded(*buf & 0x7f);
		//now in BCD mode
		if (!mDS1287.m2412) {
			
			if (t == 0x12)
				t = 0;
			if (*buf & 0x80)
				t += 0x12;
		}
		
		*reg = (*reg &~ (RTC_ALRMAR_HU | RTC_ALRMAR_HT)) | ((uint32_t)t) << RTC_ALRMAR_HU_Pos;
	}
	else {
		
		t = (*reg & (RTC_ALRMAR_HU | RTC_ALRMAR_HT)) >> RTC_ALRMAR_HU_Pos;
		//in BCD mode
		
		if (!mDS1287.m2412) {
			
			if (t == 0x00)
				t = 0x12;
			else if (t == 0x12)
				t = 0x92;
			else if (t > 0x12)
				t = 0x80 + t - 0x12;
		}
		
		*buf = (t & 0x80) | ds1287prvFromBcdIfNeeded(t & 0x7f);
	}
}

static bool ds1287prvMemAccess(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	uint_fast8_t val;
		
	pa &= 0x00ffffff;
	
#ifdef ULTRIX_SUPPORT
	//check for ESAR (Ethernet Station Address ROM) access
	if (esarMemAccess(pa, size, write, buf))
		return true;
#endif
	
	if (size != 1 || (pa & 3))
		return false;
	pa /= 4;
	if (pa >= 0x80)
		return false;
	
	switch (pa) {
		case 0x00:		//seconds
			ds1287prvSecondsAccess(&RTC->TR, buf, write);
			break;
			
		case 0x01:		//alarm seconds
			ds1287prvSecondsAccess(&RTC->ALRMAR, buf, write);
			break;
		
		case 0x02:		//minutes
			ds1287prvMinutesAccess(&RTC->TR, buf, write);
			break;
			
		case 0x03:		//alarm minutes
			ds1287prvMinutesAccess(&RTC->ALRMAR, buf, write);
			break;
	
		case 0x04:		//hours
			ds1287prvHoursAccess(&RTC->TR, buf, write);
			break;
		
		case 0x05:		//alarm hours
			ds1287prvHoursAccess(&RTC->ALRMAR, buf, write);
			break;
	
		case 0x06:		//DoW
			ds1287prvSimpleAccess(&RTC->DR, buf, write, RTC_DR_WDU, RTC_DR_WDU_Pos);
			break;
		
		case 0x07:		//date
			ds1287prvSimpleAccess(&RTC->DR, buf, write, RTC_DR_DU | RTC_DR_DT, RTC_DR_DU_Pos);
			break;
	
		case 0x08:		//month
			ds1287prvSimpleAccess(&RTC->DR, buf, write, RTC_DR_MU | RTC_DR_MT, RTC_DR_MU_Pos);
			break;
		
		case 0x09:		//year
			ds1287prvSimpleAccess(&RTC->DR, buf, write, RTC_DR_YU | RTC_DR_YT, RTC_DR_YU_Pos);
			break;
	
		case 0x0a:		//CTRLA
			if (write) {
				static const uint16_t mCntTops[] = {0xffff /* as rare as possible */, 124, 249, 3, 7, 15, 30, 62, 124, 249, 499, 999, 1999, 3999, 7999, 15999, };
				
				val = *(const uint8_t*)buf;
				
				mDS1287.dv = (val & 0x70) >> 4;
				mDS1287.rs = val = val & 0x0f;
				
				LPTIM1->ARR = mCntTops[val];
			}
			else
				*(uint8_t*)buf = (mDS1287.dv << 4) | mDS1287.rs;	//uip is clear always for us
			break;
	
		case 0x0b:		//CTRLB
			if (write) {
				
				val = *(const uint8_t*)buf;
				
				if ((val & RTC_CTRLB_SET) && !((RTC->ICSR & RTC_ICSR_INITF))) {		//go to set mode
					
					RTC->ICSR |= RTC_ICSR_INIT;
					while (!(RTC->ICSR & RTC_ICSR_INITF));
				}
				else if (!(val & RTC_CTRLB_SET) && ((RTC->ICSR & RTC_ICSR_INITF))) {	//leave set mode
					
					RTC->ICSR &=~ RTC_ICSR_INIT;
					while (RTC->ICSR & RTC_ICSR_INITF);
				}
				
				mDS1287.pie = !!(val & RTC_CTRLB_PIE);
				mDS1287.aie = !!(val & RTC_CTRLB_AIE);
				mDS1287.uie = !!(val & RTC_CTRLB_UIE);
				mDS1287.sqwe = !!(val & RTC_CTRLB_SQWE);
				mDS1287.sqwe = !!(val & RTC_CTRLB_SQWE);
				mDS1287.dm = !!(val & RTC_CTRLB_DM);
				mDS1287.m2412 = !!(val & RTC_CTRLB_2412);
				mDS1287.dse = !!(val & RTC_CTRLB_DSE);
				
				//TODO
				if (mDS1287.pie)
					LPTIM1->IER |= LPTIM_IER_ARRMIE;
				else
					LPTIM1->IER &=~ LPTIM_IER_ARRMIE;
					
				if (mDS1287.uie)
					RTC->CR |= RTC_CR_WUTE | RTC_CR_WUTIE;
				else
					RTC->CR &=~ (RTC_CR_WUTE | RTC_CR_WUTIE);
				
				if (mDS1287.aie)
					RTC->CR |= RTC_CR_ALRAE | RTC_CR_ALRAIE;
				else
					RTC->CR &=~ (RTC_CR_ALRAE | RTC_CR_ALRAIE);
			}
			else {
				
				val = 0;
				
				if (RTC->ICSR & RTC_ICSR_INITF)
					val |= RTC_CTRLB_SET;
				if (mDS1287.pie)
					val |= RTC_CTRLB_PIE;
				if (mDS1287.aie)
					val |= RTC_CTRLB_AIE;
				if (mDS1287.uie)
					val |= RTC_CTRLB_UIE;
				if (mDS1287.sqwe)
					val |= RTC_CTRLB_SQWE;
				if (mDS1287.dm)
					val |= RTC_CTRLB_DM;
				if (mDS1287.m2412)
					val |= RTC_CTRLB_2412;
				if (mDS1287.dse)
					val |= RTC_CTRLB_DSE;
				
				*(uint8_t*)buf = val;
			}
			break;

		case 0x0c:		//CTRLC
			if (!write) {
				
				val = 0;
				
				asm volatile("cpsid i");
				if (mDS1287.irqf)
					val |= RTC_CTRLC_IRQF;
				if (mDS1287.uf)
					val |= RTC_CTRLC_UF;
				if (mDS1287.af)
					val |= RTC_CTRLC_AF;
				if (mDS1287.pf)
					val |= RTC_CTRLC_PF;
				
				mDS1287.irqf = 0;
				mDS1287.uf = 0;
				mDS1287.af = 0;
				mDS1287.pf = 0;
				
				cpuIrq(SOC_IRQNO_RTC, false);
				
				asm volatile("cpsie i");
				
				*(uint8_t*)buf = val;
			}
			break;

		case 0x0d:		//CTRLD
			if (!write)
				*(uint8_t*)buf = RTC_CTRLD_VRT;
			break;
		
		default:
			if (write)
				mDS1287.ram[pa - 0x0e] = *(const uint8_t*)buf;
			else
				*(uint8_t*)buf = mDS1287.ram[pa - 0x0e];
			break;
	}
	
	return true;
}

void RTC_TAMP_IRQHandler(void)	//G0
{
	uint32_t sr = RTC->SR & (RTC_SR_ALRAF | RTC_SR_WUTF);
	bool newIrq = false;
	
	RTC->SCR = sr;
	
	if ((sr & RTC_SR_ALRAF) && !mDS1287.af) {
			
		mDS1287.af = 1;
		if (mDS1287.aie)
			newIrq = true;
	}
	
	if ((sr & RTC_SR_WUTF) && !mDS1287.uf) {
			
		mDS1287.uf = 1;
		if (mDS1287.uie)
			newIrq = true;
	}
	
	if (newIrq && !mDS1287.irqf) {
		
		mDS1287.irqf = 1;
		cpuIrq(SOC_IRQNO_RTC, true);
	}
}

void RTC_WKUP_IRQHandler(void)	//H7
{
	RTC_TAMP_IRQHandler();
}

void RTC_Alarm_IRQHandler(void)	//H7
{
	RTC_TAMP_IRQHandler();
}

void LPTIM1_IRQHandler(void)
{
	LPTIM1->ICR = LPTIM_ICR_ARRMCF;
	
	if (!mDS1287.pf) {
			
		mDS1287.pf = 1;
		if (mDS1287.pie && !mDS1287.irqf) {
			mDS1287.irqf = 1;
			cpuIrq(SOC_IRQNO_RTC, true);
		}
	}
}

bool ds1287init(void)
{
	uint32_t time;
	
	
	//init data
	mDS1287.dm = 1;
	mDS1287.m2412 = 1;
	
	#if defined(CPU_STM32G0)
		RCC->APBENR1 |= RCC_APBENR1_RTCAPBEN | RCC_APBENR1_LPTIM1EN;
	#elif defined(CPU_CM7)
		RCC->APB4ENR |= RCC_APB4ENR_RTCAPBEN;
		RCC->APB1LENR |= RCC_APB1LENR_LPTIM1EN;
	#endif
	

	//enable LSI
	RCC->CSR |= RCC_CSR_LSION;
	while(!(RCC->CSR & RCC_CSR_LSIRDY));
	
	//unlock backup domain
	PWR->CR1 |= PWR_CR1_DBP;
	
	//reset backup domain
	RCC->BDCR = RCC_BDCR_BDRST;
	RCC->BDCR &=~ RCC_BDCR_BDRST;
	
	//enable RTC clock and turn RTC on
	RCC->BDCR = RCC_BDCR_RTCSEL_1;
	RCC->BDCR |= RCC_BDCR_RTCEN;
	
	//unlock RTC regs
	RTC->WPR = 0xca;
	RTC->WPR = 0x53;
	
	//get RTC into init mode
	RTC->ICSR |= RTC_ICSR_INIT;
	while (!(RTC->ICSR & RTC_ICSR_INITF));
	
	//set prescaler freq. scale from 32.0KHz. Even if only one of the two fields needs to be changed, 2 separate write accesses must be performed to the RTC_PRER register.
	RTC->PRER = 0x007f00f9;
	RTC->PRER = 0x007f00f9;
	
	//set time
	RTC->TR = 0x00195700;	//7:57:00 PM
	RTC->DR = 0x0022a101;	//Friday jan 1, 2022
	
	//set up alarm regs for our use
	RTC->ALRMAR = RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK2 | RTC_ALRMAR_MSK1;
	
	//set up IRQs to interrupt on alarm and every second
	RTC->WUTR = 0;
	RTC->CR = (RTC->CR &~ RTC_CR_WUCKSEL) | RTC_CR_WUCKSEL_2;
	
	//go go gadget RTC!
	RTC->ICSR &=~ RTC_ICSR_INIT;
	while (RTC->ICSR & RTC_ICSR_INITF);
	time = RTC->TR;
	
	//set up LPTIM1's clock base to be LSI
	#if defined(CPU_STM32G0)
		RCC->CCIPR = (RCC->CCIPR &~ RCC_CCIPR_LPTIM1SEL) | RCC_CCIPR_LPTIM1SEL_0;
	#elif defined(CPU_CM7)
		RCC->CDCCIP2R = (RCC->CDCCIP2R &~ RCC_CDCCIP2R_LPTIM1SEL) | RCC_CDCCIP2R_LPTIM1SEL_2;
	#endif
	
	//prepare LPTIM1
	LPTIM1->CR = LPTIM_CR_ENABLE;
	while (!(LPTIM1->CR & LPTIM_CR_ENABLE));
	
	LPTIM1->CFGR = 0;	//REALTIME
	
	LPTIM1->IER = 0;
	LPTIM1->CMP = 1;
	LPTIM1->ARR = 1024;	//why not?
	LPTIM1->CR |= LPTIM_CR_CNTSTRT;
	
	//wait for RTC to go live if it has not yet
	while (RTC->TR == time) {
	
		//nothing
	}
	
	//irqs on
	#if defined(CPU_STM32G0)
		NVIC_EnableIRQ(RTC_TAMP_IRQn);
	#else
		NVIC_EnableIRQ(RTC_WKUP_IRQn);
		NVIC_EnableIRQ(RTC_Alarm_IRQn);
	#endif
	NVIC_EnableIRQ(LPTIM1_IRQn);
	
	//add memory
	return memRegionAdd(0x1d000000, 0x01000000, ds1287prvMemAccess);
}






