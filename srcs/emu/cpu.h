/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _CPU_H_
#define _CPU_H_

struct Cpu;

#include <stdbool.h>
#include <stdint.h>



#define MIPS_REG_ZERO	0	//always zero
#define MIPS_REG_AT	1	//assembler use (caller saved)
#define MIPS_REG_V0	2	//return val 0 (caller saved)
#define MIPS_REG_V1	3	//return val 1 (caller saved)
#define MIPS_REG_A0	4	//arg 0 (callee saved)
#define MIPS_REG_A1	5	//arg 1 (callee saved)
#define MIPS_REG_A2	6	//arg 2 (callee saved)
#define MIPS_REG_A3	7	//arg 3 (callee saved)
#define MIPS_REG_T0	8	//temporary 0 (caller saved)
#define MIPS_REG_T1	9	//temporary 1 (caller saved)
#define MIPS_REG_T2	10	//temporary 2 (caller saved)
#define MIPS_REG_T3	11	//temporary 3 (caller saved)
#define MIPS_REG_T4	12	//temporary 4 (caller saved)
#define MIPS_REG_T5	13	//temporary 5 (caller saved)
#define MIPS_REG_T6	14	//temporary 6 (caller saved)
#define MIPS_REG_T7	15	//temporary 7 (caller saved)
#define MIPS_REG_S0	16	//saved 0 (callee saved)
#define MIPS_REG_S1	17	//saved 1 (callee saved)
#define MIPS_REG_S2	18	//saved 2 (callee saved)
#define MIPS_REG_S3	19	//saved 3 (callee saved)
#define MIPS_REG_S4	20	//saved 4 (callee saved)
#define MIPS_REG_S5	21	//saved 5 (callee saved)
#define MIPS_REG_S6	22	//saved 6 (callee saved)
#define MIPS_REG_S7	23	//saved 7 (callee saved)
#define MIPS_REG_T8	24	//temporary 8 (caller saved)
#define MIPS_REG_T9	25	//temporary 9 (caller saved)
#define MIPS_REG_K0	26	//kernel use 0 (??? saved)
#define MIPS_REG_K1	27	//kernel use 0 (??? saved)
#define MIPS_REG_GP	28	//globals pointer (??? saved)
#define MIPS_REG_SP	29	//stack pointer (??? saved)
#define MIPS_REG_FP	30	//frame pointer (??? saved)
#define MIPS_REG_RA	31	//return address (??? saved)
#define MIPS_NUM_REGS	32	//must be power of 2
#define MIPS_EXT_REG_PC		(MIPS_NUM_REGS + 0)	//ONLY FOR external use
#define MIPS_EXT_REG_HI		(MIPS_NUM_REGS + 1)	//ONLY FOR external use
#define MIPS_EXT_REG_LO		(MIPS_NUM_REGS + 2)	//ONLY FOR external use
#define MIPS_EXT_REG_VADDR	(MIPS_NUM_REGS + 3)	//ONLY FOR external use
#define MIPS_EXT_REG_CAUSE	(MIPS_NUM_REGS + 4)	//ONLY FOR external use
#define MIPS_EXT_REG_STATUS	(MIPS_NUM_REGS + 5)	//ONLY FOR external use
#define MIPS_EXT_REG_NTRYLO	(MIPS_NUM_REGS + 6)	//ONLY FOR external use
#define MIPS_EXT_REG_NTRYHI	(MIPS_NUM_REGS + 7)	//ONLY FOR external use


#define CP0_EXC_COD_IRQ			0	//IRQ happened
#define CP0_EXC_COD_MOD			1	//TLB modified:	store to a valid entry with D bit clear
#define CP0_EXC_COD_TLBL		2	//TLB exception on load: no matching entry found, it was a load or an instruction fetch
#define CP0_EXC_COD_TLBS		3	//TLB exception on store: no matching entry found, it was a store
#define CP0_EXC_COD_ADEL		4	//unaligned access or user access to kernel map, it was a load or an instruction fetch
#define CP0_EXC_COD_ADES		5	//unaligned access or user access to kernel map, it was a store
#define CP0_EXC_COD_IBE			6	//bus error on instruction fetch
#define CP0_EXC_COD_DBE			7	//bus error on data access
#define CP0_EXC_COD_SYS			8	//syscall
#define CP0_EXC_COD_BP			9	//BREAK instr
#define CP0_EXC_COD_RI			10	//invalid instr
#define CP0_EXC_COD_CPU			11	//coprocessor unusable
#define CP0_EXC_COD_OV			12	//arith overflow
#define CP0_EXC_COD_TR			13	//TRAP
#define CP0_EXC_COD_MSAFPE		14	//MSA Floating-Point exception
#define CP0_EXC_COD_FPE			15	//Floating-Point exception
#define CP0_EXC_COD_TLBRI		19	//read inhibit caught a read
#define CP0_EXC_COD_TLBXI		20	//execution inhibit caught an attempt
#define CP0_EXC_COD_MSADIS		21	//MSA Disabled exception
#define CP0_EXC_COD_WATCH		23	//watchpoint hit



#define MIPS_HYPERCALL	0x4f646776


#define ORDER_NUM_TLB_ENTRIES	6
#define NUM_TLB_ENTRIES			(1 << ORDER_NUM_TLB_ENTRIES)		//no wired entries

#define ICACHE_NUM_WAYS_ORDER	0
#define ICACHE_NUM_WAYS			(1 << ICACHE_NUM_WAYS_ORDER)	//number of lines a given va can be in

#define ICACHE_NUM_SETS			(1 << ICACHE_NUM_SETS_ORDER)	//number of buckets of VAs
#define ICACHE_LINE_SZ_ORDER	5
#define ICACHE_LINE_SIZE		(1 << ICACHE_LINE_SZ_ORDER)
#define ICACHE_LINE_STOR_SZ		(ICACHE_LINE_SIZE + 4)


#define ORDER_NUM_TLB_BUCKETS	7
#define NUM_TLB_BUCKETS			(1 << ORDER_NUM_TLB_BUCKETS)

struct CPU {
	
//0x00:
	uint32_t regs[32];	//required to be first
//0x80:
	uint32_t space;	//for loads and stores, also stores ram amount between init and cycle and between saveState/restoreState
	uint8_t inDelaySlot;
	uint8_t haveNoIrqs;	//MUST be either 0 or 4  (0 if we have an irq, 4 if we do not)
	uint8_t llBit;
	uint8_t rfu;		//for irq stuffs
	uint32_t pc, npc;	//not always valid. also used by jit for temp storage
//0x90:
	uint32_t lo, hi, index, cause;
//0xa0:
	uint32_t status, epc, badva, entryHi;
//0xb0:
	uint32_t entryLo, context, random, memLimit;
//0xc0:
	//tlb
	struct TlbEntry {
		uint32_t va;	//top-aligned, bottom zero
		uint32_t pa;	//top-aligned, bottom zero
		uint8_t asid;	//in proper bit place, all other parts zeroes
		union{
			struct {
				
				uint8_t	enabled :1;	//cache of "asid == curAsid || g"
				uint8_t rfu		:3;
				uint8_t g		:1;
				uint8_t v		:1;
				uint8_t d		:1;
				uint8_t n		:1;
			};
			uint8_t flagsAsByte;
		};
		uint8_t prevIdx;	//element index if top bit clear, bucket index if this is the first element
		uint8_t nextIdx;	//0xff if this is the last element
	} tlb[NUM_TLB_ENTRIES];		//0x0c each
	
//0x4c0:
	uint8_t hashBuckets[NUM_TLB_BUCKETS];	//0xff if empty
	
//0x540:
	struct {
		uint8_t icache[ICACHE_LINE_SIZE];
		uint32_t addr;	//kept as LSRed by ICACHE_LINE_SIZE, so 0xfffffffe is a valid "empty "sentinel
	} ic[ICACHE_NUM_WAYS * ICACHE_NUM_SETS];
};




void cpuInit(uint32_t ramAmount);			//ram amount is advisory
void cpuCycle(void);
void cpuIrq(uint_fast8_t idx, bool raise);	//unraise when acknowledged

//for debugging
enum CpuMemAccessType {
	CpuAccessAsKernel,
	CpuAccessAsUser,
	CpuAccessAsCurrent,	//one of the above, picked based on current state
};

uint32_t cpuGetRegExternal(uint8_t reg);
void cpuSetRegExternal(uint8_t reg, uint32_t val);
bool cpuMemAccessExternal(void *buf, uint32_t va, uint_fast8_t sz, bool write, enum CpuMemAccessType type);

uint32_t cpuGetCyCnt(void);

//provided externally
bool cpuExtHypercall(void);


#endif

