/**
 * Mupen64 - savestates.c
 * Copyright (C) 2002 Hacktarux
 * Copyright (C) 2008, 2009 emu_kidid
 *
 * Mupen64 homepage: http://mupen64.emulation64.com
 * email address: hacktarux@yahoo.fr
 *                emukidid@gmail.com
 * 
 * If you want to contribute to the project please contact
 * me first (maybe someone is already making what you are
 * planning to do).
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
**/ 

#include <zlib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gccore.h>
#include "savestates.h"
#include "guifuncs.h"
#include "rom.h"
#include "../gc_memory/memory.h"
#include "../gc_memory/flashram.h"
#include "../r4300/macros.h"
#include "../r4300/r4300.h"
#include "../r4300/interupt.h"
#include "../gc_memory/TLB-Cache.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "wii64config.h"

char* statespath = "/wii64/saves/";
void LoadingBar_showBar(float percent, const char* string);
#define SAVE_STATE_MSG "Saving State .."
#define LOAD_STATE_MSG "Loading State .."

extern int *autoinc_save_slot;
void pauseAudio(void);
void resumeAudio(void);

extern BOOL hasLoadedROM;
static unsigned int savestates_slot = 0;
void savestates_select_slot(unsigned int s)
{
	if (s > 9) return;
	savestates_slot = s;
}
	
//returns 0 on file not existing
int savestates_exists(int mode)
{
	gzFile f;
	char *filename;
	filename = malloc(1024);
#ifdef HW_RVL
	sprintf(filename, "%s%s%s%s.st%d",(saveStateDevice==SAVESTATEDEVICE_USB)?"usb:":"sd:",
			statespath, ROM_SETTINGS.goodname, saveregionstr(),savestates_slot);
#else
	sprintf(filename, "sd:%s%s%s.st%d", statespath, ROM_SETTINGS.goodname, saveregionstr(),savestates_slot);
#endif

	f = gzopen(filename, (mode == SAVESTATE) ? "wb" : "rb");
	free(filename);
   	
	if(!f) {
		return 0;
	}
	gzclose(f);
	return 1;
}

void savestates_save()
{ 
	gzFile f;
	char *filename, buf[1024];
	int len, i;
	
	/* fix the filename to %s.st%d format */
	filename = malloc(1024);
#ifdef HW_RVL
	sprintf(filename, "%s%s%s%s.st%d",(saveStateDevice==SAVESTATEDEVICE_USB)?"usb:":"sd:",
			statespath, ROM_SETTINGS.goodname, saveregionstr(),savestates_slot);
#else
	sprintf(filename, "sd:%s%s%s.st%d", statespath, ROM_SETTINGS.goodname, saveregionstr(),savestates_slot);
#endif

	f = gzopen(filename, "wb");
	free(filename);
   	
	if(!f) {
		return;
	}
	LoadingBar_showBar(0.0f, SAVE_STATE_MSG);
	gzwrite(f, &rdram_register, sizeof(RDRAM_register));
	gzwrite(f, &MI_register, sizeof(mips_register));
	gzwrite(f, &pi_register, sizeof(PI_register));
	gzwrite(f, &sp_register, sizeof(SP_register));
	gzwrite(f, &rsp_register, sizeof(RSP_register));
	gzwrite(f, &si_register, sizeof(SI_register));
	gzwrite(f, &vi_register, sizeof(VI_register));
	gzwrite(f, &ri_register, sizeof(RI_register));
	gzwrite(f, &ai_register, sizeof(AI_register));
	gzwrite(f, &dpc_register, sizeof(DPC_register));
	gzwrite(f, &dps_register, sizeof(DPS_register));
	LoadingBar_showBar(0.10f, SAVE_STATE_MSG);
#ifdef USE_EXPANSION
	gzwrite(f, rdram, 0x800000);
#else
	gzwrite(f, rdram, 0x400000);
#endif
	LoadingBar_showBar(0.50f, SAVE_STATE_MSG);
	gzwrite(f, SP_DMEM, 0x1000);
	gzwrite(f, SP_IMEM, 0x1000);
	gzwrite(f, PIF_RAM, 0x40);
	
	save_flashram_infos(buf);
	gzwrite(f, buf, 24);
#ifndef USE_TLB_CACHE
	gzwrite(f, tlb_LUT_r, 0x100000);		
	gzwrite(f, tlb_LUT_w, 0x100000);
#else
	//Traverse the TLB cache hash	and dump it
	TLBCache_dump_r(f);
	TLBCache_dump_w(f);
#endif
	LoadingBar_showBar(0.85f, SAVE_STATE_MSG);

	gzwrite(f, &r4300.llbit, 4);
	gzwrite(f, r4300.gpr, 32*8);
	for (i=0; i<32; i++) gzwrite(f, r4300.reg_cop0+i, 8); // *8 for compatibility with old versions purpose
	gzwrite(f, &r4300.lo, 8);
	gzwrite(f, &r4300.hi, 8);
	
	if ((Status & 0x04000000) == 0)
	{   // FR bit == 0 means 32-bit (MIPS I) FGR mode
		shuffle_fpr_data(0, 0x04000000);  // shuffle data into 64-bit register format for storage
		gzwrite(f, r4300.fpr_data, 32*8);
		shuffle_fpr_data(0x04000000, 0);  // put it back in 32-bit mode
	}
	else
	{
		gzwrite(f, r4300.fpr_data, 32*8);
	}
	
	gzwrite(f, &r4300.fcr0, 4);
	gzwrite(f, &r4300.fcr31, 4);
	gzwrite(f, tlb_e, 32*sizeof(tlb));
	gzwrite(f, &r4300.pc, 4);    //Dynarec should be ok with just this

	gzwrite(f, &r4300.next_interrupt, 4);
	gzwrite(f, &next_vi, 4);
	gzwrite(f, &vi_field, 4);
	
	len = save_eventqueue_infos(buf);
	gzwrite(f, buf, len);
	gzclose(f);
	LoadingBar_showBar(1.0f, SAVE_STATE_MSG);
}

void savestates_load()
{
	gzFile f = NULL;
	char *filename, buf[1024];
	int len, i;
		
	/* fix the filename to %s.st%d format */
	filename = malloc(1024);
#ifdef HW_RVL
	sprintf(filename, "%s%s%s%s.st%d",(saveStateDevice==SAVESTATEDEVICE_USB)?"usb:":"sd:",
			statespath, ROM_SETTINGS.goodname, saveregionstr(),savestates_slot);
#else
	sprintf(filename, "sd:%s%s%s.st%d", statespath, ROM_SETTINGS.goodname, saveregionstr(),savestates_slot);
#endif
	
	f = gzopen(filename, "rb");
	free(filename);
	
	if (!f) {
		return;
	}
	LoadingBar_showBar(0.0f, LOAD_STATE_MSG);
	gzread(f, &rdram_register, sizeof(RDRAM_register));
	gzread(f, &MI_register, sizeof(mips_register));
	gzread(f, &pi_register, sizeof(PI_register));
	gzread(f, &sp_register, sizeof(SP_register));
	gzread(f, &rsp_register, sizeof(RSP_register));
	gzread(f, &si_register, sizeof(SI_register));
	gzread(f, &vi_register, sizeof(VI_register));
	gzread(f, &ri_register, sizeof(RI_register));
	gzread(f, &ai_register, sizeof(AI_register));
	gzread(f, &dpc_register, sizeof(DPC_register));
	gzread(f, &dps_register, sizeof(DPS_register));
	LoadingBar_showBar(0.10f, LOAD_STATE_MSG);
#ifdef USE_EXPANSION
	gzread(f, rdram, 0x800000);
#else
	gzread(f, rdram, 0x400000);
#endif
	gzread(f, SP_DMEM, 0x1000);
	gzread(f, SP_IMEM, 0x1000);
	gzread(f, PIF_RAM, 0x40);
	gzread(f, buf, 24);
	load_flashram_infos(buf);
	
#ifndef USE_TLB_CACHE
	gzread(f, tlb_LUT_r, 0x100000);
	gzread(f, tlb_LUT_w, 0x100000);
#else
	LoadingBar_showBar(0.5f, LOAD_STATE_MSG);
	int numNodesWritten_r=0,numNodesWritten_w=0,cntr,tlbpage,tlbvalue;	
	TLBCache_deinit();
	TLBCache_init();
	//Load number of them..
	gzread(f, &numNodesWritten_r, 4);
	for(cntr=0;cntr<numNodesWritten_r;cntr++)
	{
		gzread(f, &tlbpage, 4);
		gzread(f, &tlbvalue, 4);
		TLBCache_set_r(tlbpage,tlbvalue);
	}
	gzread(f, &numNodesWritten_w, 4);
	for(cntr=0;cntr<numNodesWritten_w;cntr++)
	{
		gzread(f, &tlbpage, 4);
		gzread(f, &tlbvalue, 4);
		TLBCache_set_w(tlbpage,tlbvalue);
	}
#endif
	LoadingBar_showBar(0.85f, LOAD_STATE_MSG);
	gzread(f, &r4300.llbit, 4);
	gzread(f, r4300.gpr, 32*8);
	for (i=0; i<32; i++) 
	{
		gzread(f, r4300.reg_cop0+i, 4);
		gzread(f, buf, 4); // for compatibility with old versions purpose
	}
	set_fpr_pointers(Status);  // Status is r4300.reg_cop0[12]
	gzread(f, &r4300.lo, 8);
	gzread(f, &r4300.hi, 8);
	gzread(f, r4300.fpr_data, 32*8);
	if ((Status & 0x04000000) == 0)  // 32-bit FPR mode requires data shuffling because 64-bit layout is always stored in savestate file
		shuffle_fpr_data(0x04000000, 0);
	gzread(f, &r4300.fcr0, 4);
	gzread(f, &r4300.fcr31, 4);
	gzread(f, tlb_e, 32*sizeof(tlb));
	gzread(f, &r4300.pc, 4);       //dynarec should be ok with just this
	gzread(f, &r4300.next_interrupt, 4);
	gzread(f, &next_vi, 4);
	gzread(f, &vi_field, 4);
	
	len = 0;
	while(1)
	{
		gzread(f, buf+len, 4);
		if (*((unsigned long*)&buf[len]) == 0xFFFFFFFF) break;
		gzread(f, buf+len+4, 4);
		len += 8;
	}
	load_eventqueue_infos(buf);
	gzclose(f);
	LoadingBar_showBar(1.0f, LOAD_STATE_MSG);
}
