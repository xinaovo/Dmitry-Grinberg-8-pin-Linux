/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "printf.h"
#include "ufat.h"

#define UFAT_FLAG_VOLUME_LABEL	8
#define UFAT_FLAG_DEVICE		64
#define UFAT_FLAG_RESERVED		128
#define UFAT_FLAG_LFN			(UFAT_FLAG_VOLUME_LABEL | UFAT_FLAG_READONLY | FAT_FLAG_HIDDEN | UFAT_FLAG_SYSTEM)

#define EOC_16						0xfff8
#define EOC_12						0xff8
#define CLUS_INVALID				0xffff

//fat16 only, very very limited

static uint32_t diskOffset = 0;		//to beginning of fs
static uint8_t secPerClus;
static uint16_t rootDirEntries;
static uint16_t sectorsPerFat;
static uint16_t fatSec;		//where fat begin
static uint16_t rootSec;		//where root directory begins
static uint16_t dataSec;		//where data begins
static uint16_t curClus = CLUS_INVALID;


static bool ufatParsePartitionTable(void){

	uint8_t record[16];
	uint16_t offset;

	if(diskOffset) return false;	//partitions inside partitions do not exist, probbay no fat FS on this disk - bail out

	for(offset = 0x1BE; offset < 0x1FE; offset += 16){

		if(!ufatExtRead(0, offset, 16, record)) return false;
		if(record[4] != 1 && record[4] != 4 && record[4] != 6 && record[4] != 0x0B && record[4] != 0x0C && record[4] != 0x0E) continue;	//not FAT parition

		//we now have a contender - try to mount it
		diskOffset = record[11];
		diskOffset = (diskOffset << 8) | record[10];
		diskOffset = (diskOffset << 8) | record[9];
		diskOffset = (diskOffset << 8) | record[8];
		if(ufatMount()) return true;
	}
	//if we got here, we failed - give up and cry
	return false;
}

static uint16_t ufatGetU16(const uint8_t* v, uint8_t idx){

	v += idx;
	return (((uint16_t)v[1]) << 8) | ((uint16_t)v[0]);
}

static uint32_t ufatGetU32(const uint8_t* v, uint8_t idx){

	v += idx;
	return (((uint32_t)v[3]) << 24) | (((uint32_t)v[2]) << 16) | (((uint32_t)v[1]) << 8) | ((uint32_t)v[0]);
}

bool ufatMount(void){

	uint8_t buf[13];

	if(!ufatExtRead(diskOffset, 0x36, 4, buf)) return false;
	if(buf[0] !='F' || buf[1] !='A' || buf[2] != 'T' || buf[3] != '1'){	//may be a partition table

		return ufatParsePartitionTable();
	}

	if(!ufatExtRead(diskOffset, 0x0B, 13, buf)) return false;
	if(ufatGetU16(buf, 0x0B - 0x0B) != 512) return false;		//only 512 bytes/sector FSs supported
	secPerClus = buf[0x0D - 0x0B];
	fatSec = ufatGetU16(buf, 0x0E - 0x0B);	//"reserved sectors" = sectors before first fat
	rootDirEntries = ufatGetU16(buf, 0x11 - 0x0B);
	sectorsPerFat = ufatGetU16(buf, 0x16 - 0x0B);
	
	rootSec = fatSec + sectorsPerFat * (uint16_t)(buf[0x10 - 0x0B]);
	dataSec = rootSec + (((uint32_t)rootDirEntries) * 32 + UFAT_DISK_SECTOR_SZ - 1) / UFAT_DISK_SECTOR_SZ;

	return true;
}

bool ufatGetNthFile(uint16_t n, char* name, uint32_t* sz, uint8_t* flags, uint16_t* id){

	uint16_t i;
	uint32_t sec = diskOffset + rootSec;
	uint16_t offset = 0;
	uint8_t buf[4];

	for(i = 0; i < rootDirEntries; i++){

		if(!ufatExtRead(sec, offset, 1, buf)) return false;
		if(buf[0] == 0) break;	//no more entries
		if(buf[0] != 0xE5 && buf[0] != 0x2E){		//we process only non-deleted, non "." and ".." entries

			if(!n--){		//we found it

				if(name){

					name[0] = (buf[0] == 0x05) ? 0xE5 : buf[0];
					if(!ufatExtRead(sec, offset + 1, 10, (uint8_t*)name + 1)) return false;
					name[11] = 0;
				}

				if(flags){

					if(!ufatExtRead(sec, offset + 0x0B, 1, flags)) return false;
				}

				if(id){

					if(!ufatExtRead(sec, offset + 0x1A, 2, buf)) return false;
					*id = ufatGetU16(buf, 0);
				}

				if(sz){

					if(!ufatExtRead(sec, offset + 0x1C, 4, buf)) return false;
					*sz = ufatGetU32(buf, 0);
				}

				return true;
			}
		}
		offset += 32;
		if(offset == UFAT_DISK_SECTOR_SZ){
			offset = 0;
			sec++;
		}
	}

	//we fail
	return false;
}

bool ufatOpen(uint16_t id){

	curClus = id;
	return true;
}

uint16_t ufatGetNextClus(uint16_t clus){

	uint8_t buf[2];
	uint32_t sec = diskOffset + fatSec;
	uint16_t offset, nextClus;

	sec += clus / (UFAT_DISK_SECTOR_SZ / 2);
	offset = (clus % (UFAT_DISK_SECTOR_SZ / 2)) * 2;

	if(!ufatExtRead(sec, offset, 2, buf)) return CLUS_INVALID;

	nextClus = ufatGetU16(buf, 0);
	if(nextClus >= EOC_16)
		nextClus = CLUS_INVALID;

	return nextClus;
}

bool ufatGetNextSectorRange(uint32_t* first, uint32_t* len){

	uint16_t next = curClus, prev;
	uint32_t t;


	if (curClus == CLUS_INVALID) return false;

	do{

		prev = next;
		next = ufatGetNextClus(prev);
	}while(next == prev + 1 && next != CLUS_INVALID);

	//prev is now the last cluster in this chain that is in sequence with previous ones
	//next is now the next cluster (not in sequence - fragment)

	t = prev + 1 - curClus;
	t *= secPerClus;
	*len = t;

	t = (curClus - 2);
	t *= secPerClus;
	t += dataSec;
	t += diskOffset;
	*first = t;

	curClus = next;

	return true;
}
