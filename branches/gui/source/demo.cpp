/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * demo.cpp
 * Basic template/demonstration of libwiigui capabilities. For a
 * full-featured app using many more extensions, check out Snes9x GX.
 ***************************************************************************/

#include <gccore.h>

#include <string.h>
#include <ogcsys.h>
#include <unistd.h>
#include <wiiuse/wpad.h>
#include <fat.h>

#include "sys.h"
#include "usbstorage.h"
#include "fatmounter.h"
#include "FreeTypeGX.h"
#include "video.h"
#include "audio.h"
#include "menu.h"
#include "input.h"
#include "filelist.h"
#include "demo.h"
#include "network/smb.h"
#include "xml/xml.h"

struct SSettingsSMB SMBSettings; 
int ExitRequested = 0;

char appPath[70];
char xmlPath[80];

void ExitApp()
{
	ShutoffRumble();
	StopGX();
	exit(0);
}

void
DefaultSMBSettings()
{
	LoadXmlFile(xmlPath,&SMBSettings);
}

int
main(int argc, char *argv[])
{	
	PAD_Init();
	
	InitVideo(); // Initialise video
	InitAudio(); // Initialize audio
	
	//s32 ret = IOS_ReloadIOS(249);
	u32 boot2version;
	ES_GetBoot2Version(&boot2version);
		
	if(boot2version < 5) 
	{
		if(!loadIOS(249)) if(!loadIOS(222)) if(!loadIOS(223)) if(!loadIOS(224)) if(!loadIOS(249)) loadIOS(36);
	}else{
		if(!loadIOS(249)) loadIOS(36);
	}
		
	WPAD_Init();	
	
	// Initialize file system
	fatInitDefault();

	//check application path
	if (argc > 0)
	{
	if(!strncmp(argv[0],"SD:/",4)){
		char * pch;
		pch=strrchr((char*)argv[0],'/');

		snprintf(appPath,pch-(char*)argv[0]+2,"%s",(char*)argv[0]);
		appPath[pch-(char*)argv[0]+2] = '\0';
		appPath[0] = 's';
		appPath[1] = 'd';
		sprintf(xmlPath,"%sconfig.xml",appPath);
		}
	else 
		{
		getcwd(appPath, 100);
		sprintf(xmlPath,"%sconfig.xml",appPath);
		}
	
	}
	//Load Settings from config.xml
	DefaultSMBSettings();	
	
	// read wiimote accelerometer and IR data
	WPAD_SetDataFormat(WPAD_CHAN_ALL,WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetVRes(WPAD_CHAN_ALL, screenwidth, screenheight);

	// Initialize font system
	InitFreeType((u8*)font_ttf, font_ttf_size);

	InitGUIThreads();
	InitNetworkThread();
	ResumeNetworkThread();

	MainMenu(MENU_BROWSE_DEVICE);
	exit(0);
}
