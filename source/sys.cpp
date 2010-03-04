#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <ogcsys.h>
#include <unistd.h>
#include <wiiuse/wpad.h>

#include "video.h"
#include "audio.h"
#include "menu.h"
#include "sys.h"
#include "mload.h"
#include "ehcmodule_elf.h"


/* Variables */
u8 shutdown = 0;
u8 reset = 0;

typedef struct _tmd_view_content_t
{
  uint32_t cid;
  uint16_t index;
  uint16_t type;
  uint64_t size;
} __attribute__((packed)) tmd_view_content_t;

typedef struct _tmd_view_t
{
	uint8_t version; // 0x0000;
	uint8_t filler[3];
	uint64_t sys_version; //0x0004
	uint64_t title_id; // 0x00c
	uint32_t title_type; //0x0014
	uint16_t group_id; //0x0018
	uint8_t reserved[0x3e]; //0x001a this is the same reserved 0x3e bytes from the tmd
	uint16_t title_version; //0x0058
	uint16_t num_contents; //0x005a
	tmd_view_content_t contents[]; //0x005c
}__attribute__((packed)) tmd_view;

bool isIOSstub(u8 ios_number) 
{ 
        u32 tmd_size, boot2version;
        tmd_view *ios_tmd; 
		
		ES_GetBoot2Version(&boot2version);
		
		if((boot2version == 5) && ( ios_number == 202 || ios_number == 222 || ios_number == 223 || ios_number == 224)) return true;
		
        ES_GetTMDViewSize(0x0000000100000000ULL | ios_number, &tmd_size); 
        if (!tmd_size) 
        { 
                //getting size failed. invalid or fake tmd for sure! 
                //gprintf("failed to get tmd for ios %d\n",ios_number); 
                return true; 
        } 
        ios_tmd = (tmd_view *)memalign( 32, (tmd_size+31)&(~31) ); 
        if(!ios_tmd) 
        { 
                //gprintf("failed to mem align the TMD struct!\n"); 
                return true; 
        } 
        memset(ios_tmd , 0, tmd_size); 
        ES_GetTMDView(0x0000000100000000ULL | ios_number, (u8*)ios_tmd , tmd_size); 
        //gprintf("IOS %d is rev %d(0x%x) with tmd size of %u and %u contents\n",ios_number,ios_tmd->title_version,ios_tmd->title_version,tmd_size,ios_tmd->num_contents); 
        /*Stubs have a few things in common: 
        - title version : it is mostly 65280 , or even better : in hex the last 2 digits are 0.  
                example : IOS 60 rev 6400 = 0x1900 = 00 = stub 
        - exception for IOS21 which is active, the tmd size is 592 bytes (or 140 with the views) 
        - the stub ios' have 1 app of their own (type 0x1) and 2 shared apps (type 0x8001). 
        eventho the 00 check seems to work fine , we'll only use other knowledge as well cause some 
        people/applications install an ios with a stub rev >_> ...*/ 
        u8 Version = ios_tmd->title_version; 
		
		if((boot2version == 5) && (ios_number == 249 || ios_number == 250) && (Version < 18)) return true;
        //version now contains the last 2 bytes. as said above, if this is 00, its a stub 
        if ( Version == 0 ) 
        { 
                if ( ( ios_tmd->num_contents == 3) && (ios_tmd->contents[0].type == 1 && ios_tmd->contents[1].type == 0x8001 && ios_tmd->contents[2].type == 0x8001) ) 
                { 
                        //gprintf("IOS %d is a stub\n",ios_number); 
                        free(ios_tmd); 
                        return true; 
                } 
                else 
                { 
                        //gprintf("IOS %d is active\n",ios_number); 
                        free(ios_tmd); 
                        return false; 
                } 
        } 
        //gprintf("IOS %d is active\n",ios_number); 
        free(ios_tmd); 
        return false; 
} 
 

bool loadIOS(int ios)
{
	if(isIOSstub(ios)) return false;
	mload_close();
	if(IOS_ReloadIOS(ios)>=0)
	{
		if (IOS_GetVersion() != 249 && IOS_GetVersion() != 250)
		{
			if (mload_init() >= 0)
			{
				data_elf my_data_elf;
				mload_elf((void *) ehcmodule_elf, &my_data_elf);
				mload_run_thread(my_data_elf.start, my_data_elf.stack, my_data_elf.size_stack, 0x47);
			}
		}
		return true;
	}
	return false;
}

void __Sys_ResetCallback(void) {
    /* Reboot console */
    //reset = 1;
	Sys_Reboot();
}

void __Sys_PowerCallback(void) {
    /* Poweroff console */
    //shutdown = 1;
	Sys_Shutdown();
}

void Sys_Init(void) {
    /* Set RESET/POWER button callback */
    SYS_SetResetCallback(__Sys_ResetCallback);
    SYS_SetPowerCallback(__Sys_PowerCallback);
}

static void _ExitApp() {
     StopGX();
    ShutdownAudio();
}

void Sys_Reboot(void) {
    /* Restart console */
    _ExitApp();
    STM_RebootSystem();
}

#define ShutdownToDefault	0
#define ShutdownToIdle		1
#define ShutdownToStandby	2

static void _Sys_Shutdown(int SHUTDOWN_MODE) {
    _ExitApp();
    /*WPAD_Flush(0);
    WPAD_Disconnect(0);
    WPAD_Shutdown();
*/
    /* Poweroff console */
    if ((CONF_GetShutdownMode() == CONF_SHUTDOWN_IDLE &&  SHUTDOWN_MODE != ShutdownToStandby) || SHUTDOWN_MODE == ShutdownToIdle) {
        s32 ret;

        /* Set LED mode */
        ret = CONF_GetIdleLedMode();
        if (ret >= 0 && ret <= 2)
            STM_SetLedMode(ret);

        /* Shutdown to idle */
        STM_ShutdownToIdle();
    } else {
        /* Shutdown to standby */
        STM_ShutdownToStandby();
    }
}
void Sys_Shutdown(void) {
    _Sys_Shutdown(ShutdownToDefault);
}
void Sys_ShutdownToIdel(void) {
    _Sys_Shutdown(ShutdownToIdle);
}
void Sys_ShutdownToStandby(void) {
    _Sys_Shutdown(ShutdownToStandby);
}

void Sys_LoadMenu(void) {
    _ExitApp();
    /* Return to the Wii system menu */
    SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

void Sys_BackToLoader(void) {
    if (*((u32*) 0x80001800)) {
        _ExitApp();
        exit(0);
    }
    // Channel Version
    Sys_LoadMenu();
}
