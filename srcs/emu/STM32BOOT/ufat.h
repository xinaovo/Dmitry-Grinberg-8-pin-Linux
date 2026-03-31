/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _UFAT_H_
#define _UFAT_H_

#include <stdbool.h>
#include <stdint.h>



#define UFAT_DISK_SECTOR_SZ		512

#define UFAT_FLAG_READONLY		1
#define UFAT_FLAG_HIDDEN		2
#define UFAT_FLAG_SYSTEM		4
#define UFAT_FLAG_VOLUME_LBL	8
#define UFAT_FLAG_DIR			16
#define UFAT_FLAG_ARCHIVE		32


//externally required function(s)
bool ufatExtRead(uint32_t sector, uint16_t offset, uint8_t len, uint8_t* buf);

bool ufatMount(void);									//try mounting a volume
bool ufatGetNthFile(uint16_t n, char* name, uint32_t* sz, uint8_t* flags, uint16_t* id);	//in root directory only, false for no more
bool ufatOpen(uint16_t id);									//in root directory only
bool ufatGetNextSectorRange(uint32_t* first, uint32_t* len);					//for currently opened file, false for "no more"

#endif
