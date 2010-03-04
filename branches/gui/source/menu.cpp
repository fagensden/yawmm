/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * menu.cpp
 * Menu flow routines - handles all menu logic
 ***************************************************************************/

#include <gccore.h>
#include <ogcsys.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fat.h>
#include <sys/stat.h>
#include <wiiuse/wpad.h>
#include <ogc/disc_io.h>
#include <sdcard/wiisd_io.h>
#include "usbstorage.h"
#include "nand/nand.h"

#include "sys.h"
#include "libwiigui/gui.h"
#include "menu.h"
#include "demo.h"
#include "input.h"
#include "filelist.h"
#include "filebrowser.h"
#include "wad/wad.h"
#include "wad/utils.h"
#include "wad/title.h"
#include "network/smb.h"
#include "fatmounter.h"
#include "fileop.h"
#include "xml/xml.h"
#include <iostream>

#define THREAD_SLEEP 100

static GuiImageData * pointer[2];
static GuiImage * bgImg = NULL;
//static GuiSound * bgMusic = NULL;
static GuiWindow * mainWindow = NULL;
static lwp_t guithread = LWP_THREAD_NULL;
static bool guiHalt = true;

extern char appPath[70];
extern char xmlPath[80];

/* device list  */
static fatDevice fdevList[] = {
	{ "sd",		"Wii SD Slot",			&__io_wiisd },
	{ "usb",	"USB Mass Storage Device",	&__io_usbstorage },
	{ "usb2",	"USB 2.0 Mass Storage Device",	&__io_wiiums },
	{ "smb",	"SMB Share",	&__io_wiisd }, //temp
};

/* NAND device list */
static nandDevice ndevList[] = {
	{ "Disable",				0,	0x00,	0x00 },
	{ "SD/SDHC Card",			1,	0xF0,	0xF1 },
	{ "USB 2.0 Mass Storage Device",	2,	0xF2,	0xF3 },
};


typedef struct {
	int version;
	int region;

} SMRegion;

SMRegion regionlist[] = {
	{33, 'X'},
	{128, 'J'}, {97, 'E'}, {130, 'P'},
	{162, 'P'},
	{192, 'J'}, {193, 'E'}, {194, 'P'},
	{224, 'J'}, {225, 'E'}, {226, 'P'},
	{256, 'J'}, {257, 'E'}, {258, 'P'},
	{288, 'J'}, {289, 'E'}, {290, 'P'},
	{352, 'J'}, {353, 'E'}, {354, 'P'}, {326, 'K'},
	{384, 'J'}, {385, 'E'}, {386, 'P'},
	{390, 'K'},
	{416, 'J'}, {417, 'E'}, {418, 'P'},
	{448, 'J'}, {449, 'E'}, {450, 'P'}, {454, 'K'},
	{480, 'J'}, {481, 'E'}, {482, 'P'}, {486, 'K'},
};

#define NB_SM		(sizeof(regionlist) / sizeof(SMRegion))

// device 
static fatDevice  *fdev = NULL;
static nandDevice *ndev = NULL;

u32 be32(const u8 *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

u64 be64(const u8 *p)
{
	return ((u64)be32(p) << 32) | be32(p + 4);
}

u64 get_title_ios(u64 title) {
	s32 ret, fd;
	static char filepath[256] ATTRIBUTE_ALIGN(32);	
	
	// Check to see if title exists
	if (ES_GetDataDir(title, filepath) >= 0 ) {
		u32 tmd_size;
		static u8 tmd_buf[MAX_SIGNED_TMD_SIZE] ATTRIBUTE_ALIGN(32);
	
		ret = ES_GetStoredTMDSize(title, &tmd_size);
		if (ret < 0){
			// If we fail to use the ES function, try reading manually
			// This is a workaround added since some IOS (like 21) don't like our
			// call to ES_GetStoredTMDSize
			
			//printf("Error! ES_GetStoredTMDSize: %d\n", ret);
					
			sprintf(filepath, "/title/%08x/%08x/content/title.tmd", TITLE_UPPER(title), TITLE_LOWER(title));
			
			ret = ISFS_Open(filepath, ISFS_OPEN_READ);
			if (ret <= 0)
			{
				//printf("Error! ISFS_Open (ret = %d)\n", ret);
				return 0;
			}
			
			fd = ret;
			
			ret = ISFS_Seek(fd, 0x184, 0);
			if (ret < 0)
			{
				//printf("Error! ISFS_Seek (ret = %d)\n", ret);
				return 0;
			}
			
			ret = ISFS_Read(fd,tmd_buf,8);
			if (ret < 0)
			{
				//printf("Error! ISFS_Read (ret = %d)\n", ret);
				return 0;
			}
			
			ret = ISFS_Close(fd);
			if (ret < 0)
			{
				//printf("Error! ISFS_Close (ret = %d)\n", ret);
				return 0;
			}
			
			return be64(tmd_buf);
			
		} else {
			// Normal versions of IOS won't have a problem, so we do things the "right" way.
			
			// Some of this code adapted from bushing's title_lister.c
			signed_blob *s_tmd = (signed_blob *)tmd_buf;
			ret = ES_GetStoredTMD(title, s_tmd, tmd_size);
			if (ret < 0){
				//printf("Error! ES_GetStoredTMD: %d\n", ret);
				return -1;
			}
			tmd *t = (tmd *)SIGNATURE_PAYLOAD(s_tmd);
			return t->sys_version;
		}
		
		
	} 
	return 0;
}

int get_sm_region_basic()
{
	u32 tmd_size;
		
	u64 title = TITLE_ID(1, 2);
	static u8 tmd_buf[MAX_SIGNED_TMD_SIZE] ATTRIBUTE_ALIGN(32);
	
	int ret = ES_GetStoredTMDSize(title, &tmd_size);
		
	// Some of this code adapted from bushing's title_lister.c
	signed_blob *s_tmd = (signed_blob *)tmd_buf;
	ret = ES_GetStoredTMD(title, s_tmd, tmd_size);
	if (ret < 0){
		//printf("Error! ES_GetStoredTMD: %d\n", ret);
		return -1;
	}
	tmd *t = (tmd *)SIGNATURE_PAYLOAD(s_tmd);
	ret = t->title_version;
	int i = 0;
	while( i <= NB_SM)
	{
		if(	regionlist[i].version == ret) return regionlist[i].region;
		i++;
	}
	return 0;
}


/****************************************************************************
 * ResumeGui
 *
 * Signals the GUI thread to start, and resumes the thread. This is called
 * after finishing the removal/insertion of new elements, and after initial
 * GUI setup.
 ***************************************************************************/
static void
ResumeGui()
{
	guiHalt = false;
	LWP_ResumeThread (guithread);
}

/****************************************************************************
 * HaltGui
 *
 * Signals the GUI thread to stop, and waits for GUI thread to stop
 * This is necessary whenever removing/inserting new elements into the GUI.
 * This eliminates the possibility that the GUI is in the middle of accessing
 * an element that is being changed.
 ***************************************************************************/
static void
HaltGui()
{
	guiHalt = true;

	// wait for thread to finish
	while(!LWP_ThreadIsSuspended(guithread))
		usleep(THREAD_SLEEP);
}

/****************************************************************************
 * WindowPrompt2
 *
 ***************************************************************************/
int
WindowPrompt2(const char *title, const char *msg, const char *btn1Label,
             const char *btn2Label, const char *btn3Label,
             const char *btn4Label) {

	int choice = -1;
    GuiWindow promptWindow(448,288);
	promptWindow.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	promptWindow.SetPosition(0, -10);
	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND_PCM);
    GuiSound btnClick(button_click_pcm, button_click_pcm_size, SOUND_PCM);

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiImageData dialogBox(dialogue_box_png);
	GuiImage dialogBoxImg(&dialogBox);

	GuiTrigger trigA;
    trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);
    GuiTrigger trigB;
    trigB.SetButtonOnlyTrigger(-1, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B);

    GuiText titleTxt(title, 26, (GXColor) {0, 0, 0, 255});
    titleTxt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
    titleTxt.SetPosition(10,40);
    titleTxt.SetMaxWidth(345);
	titleTxt.SetScroll(SCROLL_HORIZONTAL);
	
	GuiText msgTxt(msg, 24, (GXColor) {0, 0, 0, 255});
    msgTxt.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
    msgTxt.SetPosition(0,-40);
    msgTxt.SetMaxWidth(430);

    GuiText btn1Txt(btn1Label, 24, (GXColor) {0, 0, 0, 255});
    GuiImage btn1Img(&btnOutline);
    GuiButton btn1(&btn1Img, &btn1Img, 0,3,0,0,&trigA,&btnSoundOver,&btnClick,1);
    btn1.SetLabel(&btn1Txt);
	btn1.SetScale(0.8);
    btn1.SetState(STATE_SELECTED);

    GuiText btn2Txt(btn2Label, 24, (GXColor) {0, 0, 0, 255});
    GuiImage btn2Img(&btnOutline);
    GuiButton btn2(&btn2Img, &btn2Img, 0,3,0,0,&trigA,&btnSoundOver,&btnClick,1);
    btn2.SetLabel(&btn2Txt);
	btn2.SetScale(0.8);
    if (!btn3Label && !btn4Label)
        btn2.SetTrigger(&trigB);

    GuiText btn3Txt(btn3Label, 24, (GXColor) {0, 0, 0, 255});
    GuiImage btn3Img(&btnOutline);
    GuiButton btn3(&btn3Img, &btn3Img, 0,3,0,0,&trigA,&btnSoundOver,&btnClick,1);
    btn3.SetLabel(&btn3Txt);
	btn3.SetScale(0.8);
    if (!btn4Label)
        btn3.SetTrigger(&trigB);

    GuiText btn4Txt(btn4Label, 24, (GXColor) {0, 0, 0, 255});
    GuiImage btn4Img(&btnOutline);
    GuiButton btn4(&btn4Img, &btn4Img, 0,3,0,0,&trigA,&btnSoundOver,&btnClick,1);
    btn4.SetLabel(&btn4Txt);
	btn4.SetScale(0.8);
    if (btn4Label)
        btn4.SetTrigger(&trigB);

    if (btn2Label && !btn3Label && !btn4Label) {
            btn1.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
            btn1.SetPosition(40, -45);
            btn2.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
            btn2.SetPosition(-40, -45);
            btn3.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
            btn3.SetPosition(40, -65);
            btn4.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
            btn4.SetPosition(-40, -65);
        } else if (btn2Label && btn3Label && !btn4Label) {
            btn1.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
            btn1.SetPosition(40, -120);
            btn2.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
            btn2.SetPosition(-40, -120);
            btn3.SetAlignment(ALIGN_CENTRE, ALIGN_BOTTOM);
            btn3.SetPosition(0, -65);
            btn4.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
            btn4.SetPosition(-50, -65);
        } else if (btn2Label && btn3Label && btn4Label) {
            btn1.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
            btn1.SetPosition(40, -120);
            btn2.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
            btn2.SetPosition(-40, -120);
            btn3.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
            btn3.SetPosition(40, -65);
            btn4.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
            btn4.SetPosition(-40, -65);
        } else if (!btn2Label && btn3Label && btn4Label) {
            btn1.SetAlignment(ALIGN_CENTRE, ALIGN_BOTTOM);
            btn1.SetPosition(0, -120);
            btn2.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
            btn2.SetPosition(-40, -120);
            btn3.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
            btn3.SetPosition(40, -65);
            btn4.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
            btn4.SetPosition(-40, -65);
        } else {
            btn1.SetAlignment(ALIGN_CENTRE, ALIGN_BOTTOM);
            btn1.SetPosition(0, -45);
            btn2.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
            btn2.SetPosition(40, -120);
            btn3.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
            btn3.SetPosition(40, -65);
            btn4.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
            btn4.SetPosition(-40, -65);
        }

    promptWindow.Append(&dialogBoxImg);
    promptWindow.Append(&titleTxt);
    promptWindow.Append(&msgTxt);

    if (btn1Label)
        promptWindow.Append(&btn1);
    if (btn2Label)
        promptWindow.Append(&btn2);
    if (btn3Label)
        promptWindow.Append(&btn3);
    if (btn4Label)
        promptWindow.Append(&btn4);

    promptWindow.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_IN, 50);
    HaltGui();
    mainWindow->SetState(STATE_DISABLED);
    mainWindow->Append(&promptWindow);
    mainWindow->ChangeFocus(&promptWindow);
    ResumeGui();

    while (choice == -1) {
        VIDEO_WaitVSync();

        if (btn1.GetState() == STATE_CLICKED) {
            choice = 1;
        } else if (btn2.GetState() == STATE_CLICKED) {
            if (!btn3Label)
                choice = 0;
            else
                choice = 2;
        } else if (btn3.GetState() == STATE_CLICKED) {
            if (!btn4Label)
                choice = 0;
            else
                choice = 3;
        } else if (btn4.GetState() == STATE_CLICKED) {
            choice = 0;
        }
    }

    promptWindow.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 50);
    while (promptWindow.GetEffect() > 0) usleep(50);
    HaltGui();
    mainWindow->Remove(&promptWindow);
    mainWindow->SetState(STATE_DEFAULT);
    ResumeGui();
    return choice;
}


/****************************************************************************
 * WindowPrompt
 *
 * Displays a prompt window to user, with information, an error message, or
 * presenting a user with a choice
 ***************************************************************************/
int
WindowPrompt(const char *title, const char *msg, const char *btn1Label, const char *btn2Label)
{
	int choice = -1;

	GuiWindow promptWindow(448,288);
	promptWindow.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	promptWindow.SetPosition(0, -10);
	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND_PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiTrigger trigA;
	trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);

	GuiImageData dialogBox(dialogue_box_png);
	GuiImage dialogBoxImg(&dialogBox);

	GuiText titleTxt(title, 26, (GXColor){0, 0, 0, 255});
	titleTxt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	titleTxt.SetPosition(0,40);
	GuiText msgTxt(msg, 20, (GXColor){0, 0, 0, 255});
	msgTxt.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	msgTxt.SetPosition(0,-20);
	msgTxt.SetWrap(true, 400);

	GuiText btn1Txt(btn1Label, 24, (GXColor){0, 0, 0, 255});
	GuiImage btn1Img(&btnOutline);
	GuiImage btn1ImgOver(&btnOutlineOver);
	GuiButton btn1(btnOutline.GetWidth(), btnOutline.GetHeight());

	if(btn2Label)
	{
		btn1.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
		btn1.SetPosition(20, -25);
	}
	else
	{
		btn1.SetAlignment(ALIGN_CENTRE, ALIGN_BOTTOM);
		btn1.SetPosition(0, -25);
	}

	btn1.SetLabel(&btn1Txt);
	btn1.SetImage(&btn1Img);
	btn1.SetImageOver(&btn1ImgOver);
	btn1.SetSoundOver(&btnSoundOver);
	btn1.SetTrigger(&trigA);
	btn1.SetState(STATE_SELECTED);
	btn1.SetEffectGrow();

	GuiText btn2Txt(btn2Label, 24, (GXColor){0, 0, 0, 255});
	GuiImage btn2Img(&btnOutline);
	GuiImage btn2ImgOver(&btnOutlineOver);
	GuiButton btn2(btnOutline.GetWidth(), btnOutline.GetHeight());
	btn2.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
	btn2.SetPosition(-20, -25);
	btn2.SetLabel(&btn2Txt);
	btn2.SetImage(&btn2Img);
	btn2.SetImageOver(&btn2ImgOver);
	btn2.SetSoundOver(&btnSoundOver);
	btn2.SetTrigger(&trigA);
	btn2.SetEffectGrow();

	promptWindow.Append(&dialogBoxImg);
	promptWindow.Append(&titleTxt);
	promptWindow.Append(&msgTxt);
	promptWindow.Append(&btn1);

	if(btn2Label)
		promptWindow.Append(&btn2);

	promptWindow.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_IN, 50);
	HaltGui();
	mainWindow->SetState(STATE_DISABLED);
	mainWindow->Append(&promptWindow);
	mainWindow->ChangeFocus(&promptWindow);
	ResumeGui();

	while(choice == -1)
	{
		usleep(THREAD_SLEEP);

		if(btn1.GetState() == STATE_CLICKED)
			choice = 1;
		else if(btn2.GetState() == STATE_CLICKED)
			choice = 0;
	}

	promptWindow.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 50);
	while(promptWindow.GetEffect() > 0) usleep(THREAD_SLEEP);
	HaltGui();
	mainWindow->Remove(&promptWindow);
	mainWindow->SetState(STATE_DEFAULT);
	ResumeGui();
	return choice;
}


/****************************************************************************
* WAD_PopUpMenu
***************************************************************************/
int WAD_PopUpMenu(int x, int y)
{
    int choice = -1;
    int numItems = 7;
    int buttonY = 0;

    GuiImageData dialogBox(clickmenu_png);
    GuiImage dialogBoxImg(&dialogBox);
    dialogBoxImg.SetPosition(-8, -dialogBox.GetHeight()/numItems/2);

    GuiImageData menu_select(menu_selection_png);

    if(screenwidth < x + dialogBox.GetWidth() + 10)
        x = screenwidth - dialogBox.GetWidth() - 10;

    if(screenheight < y + dialogBox.GetHeight() + 10)
        y = screenheight - dialogBox.GetHeight() - 10;

    GuiWindow promptWindow(dialogBox.GetWidth(), dialogBox.GetHeight());
    promptWindow.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    promptWindow.SetPosition(x, y);

    GuiTrigger trigA;
    trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);
    GuiTrigger trigB;
    trigB.SetButtonOnlyTrigger(-1, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B);

    GuiSound btnClick(button_click_pcm, button_click_pcm_size, SOUND_PCM);

    GuiText InstallTxt("Install", 20, (GXColor){0, 0, 0, 255});
    GuiText InstallTxtOver("Install",20, (GXColor){28, 32, 190, 255});
    InstallTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    InstallTxtOver.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    GuiButton Installbtn(promptWindow.GetWidth(), promptWindow.GetHeight()/numItems);
    Installbtn.SetLabel(&InstallTxt);
    Installbtn.SetLabelOver(&InstallTxtOver);
    Installbtn.SetSoundClick(&btnClick);
    GuiImage InstallbtnSelect(&menu_select);
    Installbtn.SetImageOver(&InstallbtnSelect);
    Installbtn.SetTrigger(&trigA);
    Installbtn.SetPosition(5,buttonY);
    Installbtn.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    buttonY += promptWindow.GetHeight()/numItems;

    GuiText UninstallTxt("Uninstall", 20, (GXColor){0, 0, 0, 255});
    GuiText UninstallTxtOver("Uninstall",20, (GXColor){28, 32, 190, 255});
    UninstallTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    UninstallTxtOver.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    GuiButton Uninstallbtn(promptWindow.GetWidth(), promptWindow.GetHeight()/numItems);
    Uninstallbtn.SetLabel(&UninstallTxt);
    Uninstallbtn.SetLabelOver(&UninstallTxtOver);
    GuiImage UninstallbtnSelect(&menu_select);
    Uninstallbtn.SetImageOver(&UninstallbtnSelect);
    Uninstallbtn.SetSoundClick(&btnClick);
    Uninstallbtn.SetTrigger(&trigA);
    Uninstallbtn.SetPosition(5,buttonY);
    Uninstallbtn.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    buttonY += promptWindow.GetHeight()/numItems;

    GuiText AddBatchTxt("Add Batch",20, (GXColor){0, 0, 0, 255});
    GuiText AddBatchTxtOver("Add Batch", 20, (GXColor){28, 32, 190, 255});
    AddBatchTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    AddBatchTxtOver.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    GuiButton AddBatchbtn(promptWindow.GetWidth(), promptWindow.GetHeight()/numItems);
    AddBatchbtn.SetLabel(&AddBatchTxt);
    AddBatchbtn.SetLabelOver(&AddBatchTxtOver);
    GuiImage AddBatchbtnSelect(&menu_select);
    AddBatchbtn.SetImageOver(&AddBatchbtnSelect);
    AddBatchbtn.SetSoundClick(&btnClick);
    AddBatchbtn.SetTrigger(&trigA);
    AddBatchbtn.SetPosition(5,buttonY);
    AddBatchbtn.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    buttonY += promptWindow.GetHeight()/numItems;

    GuiText PropertiesTxt("Properties", 20, (GXColor){0, 0, 0, 255});
    GuiText PropertiesTxtOver("Properties", 20, (GXColor){28, 32, 190, 255});
    PropertiesTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    PropertiesTxtOver.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    GuiButton Propertiesbtn(promptWindow.GetWidth(), promptWindow.GetHeight()/numItems);
    Propertiesbtn.SetLabel(&PropertiesTxt);
    Propertiesbtn.SetLabelOver(&PropertiesTxtOver);
    GuiImage PropertiesbtnMenuSelect(&menu_select);
    Propertiesbtn.SetImageOver(&PropertiesbtnMenuSelect);
    Propertiesbtn.SetSoundClick(&btnClick);
    Propertiesbtn.SetTrigger(&trigA);
    Propertiesbtn.SetPosition(5,buttonY);
    Propertiesbtn.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    buttonY += promptWindow.GetHeight()/numItems;

    GuiButton NoBtn(screenwidth, screenheight);
    NoBtn.SetPosition(-x, -y);
    NoBtn.SetTrigger(&trigA);
    NoBtn.SetTrigger(&trigB);

    promptWindow.Append(&dialogBoxImg);
    promptWindow.Append(&NoBtn);
    promptWindow.Append(&Installbtn); 
    promptWindow.Append(&Uninstallbtn);
	promptWindow.Append(&AddBatchbtn);
    promptWindow.Append(&Propertiesbtn);

    HaltGui();
    mainWindow->Append(&promptWindow);
    mainWindow->ChangeFocus(&promptWindow);
    ResumeGui();

    while(choice == -1)
    {
        VIDEO_WaitVSync();

		if(Installbtn.GetState() == STATE_CLICKED) {
            choice = INSTALL;
            break;
        }
        else if(Uninstallbtn.GetState() == STATE_CLICKED) {
            choice = UNINSTALL;
            break;
        }
        else if(AddBatchbtn.GetState() == STATE_CLICKED) {
            choice = ADDBATCH;
            break;
        }
        else if(Propertiesbtn.GetState() == STATE_CLICKED) {
            choice = PROPERTIES;
            break;
        }
        else if(NoBtn.GetState() == STATE_CLICKED){
            choice = -2;
            break;
        }
    }

    HaltGui();
    mainWindow->Remove(&promptWindow);
    ResumeGui();

    return choice;
}

/****************************************************************************
 * UpdateGUI
 *
 * Primary thread to allow GUI to respond to state changes, and draws GUI
 ***************************************************************************/

static void *
UpdateGUI (void *arg)
{
	int i;

	while(1)
	{
		if(guiHalt)
		{
			LWP_SuspendThread(guithread);
		}
		else
		{
			mainWindow->Draw();

			#ifdef HW_RVL
			for(i=3; i >= 0; i--) // so that player 1's cursor appears on top!
			{
				if(userInput[i].wpad.ir.valid)
					Menu_DrawImg(userInput[i].wpad.ir.x-48, userInput[i].wpad.ir.y-48,
						96, 96, pointer[i]->GetImage(), userInput[i].wpad.ir.angle, 1, 1, 255);
				DoRumble(i);
			}
			#endif

			Menu_Render();

			for(i=0; i < 4; i++)
				mainWindow->Update(&userInput[i]);

			if(ExitRequested)
			{
				for(i = 0; i < 255; i += 15)
				{
					mainWindow->Draw();
					Menu_DrawRectangle(0,0,screenwidth,screenheight,(GXColor){0, 0, 0, i},1);
					Menu_Render();
				}
				ExitApp();
			}
		}
	}
	return NULL;
}

/****************************************************************************
 * InitGUIThread
 *
 * Startup GUI threads
 ***************************************************************************/
void
InitGUIThreads()
{
	LWP_CreateThread (&guithread, UpdateGUI, NULL, NULL, 0, 70);
}

/****************************************************************************
 * OnScreenKeyboard
 *
 * Opens an on-screen keyboard window, with the data entered being stored
 * into the specified variable.
 ***************************************************************************/
static void OnScreenKeyboard(char * var, u16 maxlen)
{
	int save = -1;

	GuiKeyboard keyboard(var, maxlen);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND_PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiTrigger trigA;
	trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);

	GuiText okBtnTxt("OK", 20, (GXColor){0, 0, 0, 255});
	GuiImage okBtnImg(&btnOutline);
	GuiImage okBtnImgOver(&btnOutlineOver);
	GuiButton okBtn(btnOutline.GetWidth(), btnOutline.GetHeight());

	okBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	okBtn.SetPosition(25, -25);

	okBtn.SetLabel(&okBtnTxt);
	okBtn.SetImage(&okBtnImg);
	okBtn.SetImageOver(&okBtnImgOver);
	okBtn.SetSoundOver(&btnSoundOver);
	okBtn.SetTrigger(&trigA);
	okBtn.SetEffectGrow();

	GuiText cancelBtnTxt("Cancel", 20, (GXColor){0, 0, 0, 255});
	GuiImage cancelBtnImg(&btnOutline);
	GuiImage cancelBtnImgOver(&btnOutlineOver);
	GuiButton cancelBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	cancelBtn.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
	cancelBtn.SetPosition(-25, -25);
	cancelBtn.SetLabel(&cancelBtnTxt);
	cancelBtn.SetImage(&cancelBtnImg);
	cancelBtn.SetImageOver(&cancelBtnImgOver);
	cancelBtn.SetSoundOver(&btnSoundOver);
	cancelBtn.SetTrigger(&trigA);
	cancelBtn.SetEffectGrow();

	keyboard.Append(&okBtn);
	keyboard.Append(&cancelBtn);

	HaltGui();
	mainWindow->SetState(STATE_DISABLED);
	mainWindow->Append(&keyboard);
	mainWindow->ChangeFocus(&keyboard);
	ResumeGui();

	while(save == -1)
	{
		usleep(THREAD_SLEEP);

		if(okBtn.GetState() == STATE_CLICKED)
			save = 1;
		else if(cancelBtn.GetState() == STATE_CLICKED)
			save = 0;
	}

	if(save)
	{
		snprintf(var, maxlen, "%s", keyboard.kbtextstr);
	}

	HaltGui();
	mainWindow->Remove(&keyboard);
	mainWindow->SetState(STATE_DEFAULT);
	ResumeGui();
}

/* Variables */
static u8 wadBuffer[BLOCK_SIZE] ATTRIBUTE_ALIGN(32);

/****************************************************************************
 * Wad_Install
 *
 * Window to install WAD and show progress
 ***************************************************************************/
char region[2] = {0x00,0x03};

static 
s32 Wad_Install(FILE *fp, bool showBtn)
{
	GuiWindow promptWindow(472,320);
	promptWindow.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	promptWindow.SetPosition(0, -10);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND_PCM);
    GuiSound btnClick(button_click_pcm, button_click_pcm_size, SOUND_PCM);

	char imgPath[100];
	GuiImageData btnOutline(imgPath, button_png);
	GuiImageData dialogBox(imgPath, dialogue_box_png);
	GuiTrigger trigA;
	trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);

	GuiImage dialogBoxImg(&dialogBox);

	GuiText btn1Txt("Ok", 20, (GXColor){0,0,0, 255});
	GuiImage btn1Img(&btnOutline);
	GuiButton btn1(&btn1Img,&btn1Img, 2, 4, 0, -45, &trigA, &btnSoundOver, &btnClick,1);
	btn1.SetLabel(&btn1Txt);
	btn1.SetScale(0.7);
	btn1.SetState(STATE_SELECTED);

	GuiImageData progressbarOutline(imgPath, progressbar_outline_png);
	GuiImage progressbarOutlineImg(&progressbarOutline);
	progressbarOutlineImg.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
	progressbarOutlineImg.SetPosition(25, 40);

	GuiImageData progressbarEmpty(imgPath, progressbar_empty_png);
	GuiImage progressbarEmptyImg(&progressbarEmpty);
	progressbarEmptyImg.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
	progressbarEmptyImg.SetPosition(25, 40);
	progressbarEmptyImg.SetTile(100);

	GuiImageData progressbar(imgPath, progressbar_png);
	GuiImage progressbarImg(&progressbar);
	progressbarImg.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
	progressbarImg.SetPosition(25, 40);

	char title[20];
	sprintf(title, "%s", "Installing WAD");
	GuiText titleTxt(title, 24, (GXColor){0, 0, 0, 255});
	titleTxt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	titleTxt.SetPosition(0,40);

	GuiText msg1Txt(NULL, 20, (GXColor){0, 0, 0, 255});
	msg1Txt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	msg1Txt.SetPosition(0,75);
	GuiText msg2Txt(NULL, 20, (GXColor){0, 0, 0, 255});
	msg2Txt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	msg2Txt.SetPosition(0, 98);

	GuiText msg3Txt(NULL, 20, (GXColor){0, 0, 0, 255});
	msg3Txt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	msg3Txt.SetPosition(0, 121);

	GuiText msg4Txt(NULL, 20, (GXColor){0, 0, 0, 255});
	msg4Txt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	msg4Txt.SetPosition(0, 144);

	promptWindow.Append(&dialogBoxImg);
	promptWindow.Append(&titleTxt);
	promptWindow.Append(&msg4Txt);
	promptWindow.Append(&msg3Txt);
	promptWindow.Append(&msg1Txt);
	promptWindow.Append(&msg2Txt);

	promptWindow.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_IN, 50);

	HaltGui();
	mainWindow->SetState(STATE_DISABLED);
	mainWindow->Append(&promptWindow);
	mainWindow->ChangeFocus(&promptWindow);

	wadHeader   *header  = NULL;
	signed_blob *p_certs = NULL, *p_crl = NULL, *p_tik = NULL, *p_tmd = NULL;

	tmd *tmd_data  = NULL;

	u32 cnt, offset = 0;
	s32 ret = 666;

	if (fp==NULL) {goto err;}

	ResumeGui();
	msg1Txt.SetText(("Reading WAD data..."));
	HaltGui();
	// WAD header
	ret = Wad_ReadAlloc(fp, (void **)&header, offset, sizeof(wadHeader));

	if (ret < 0)
			goto err;
	else
			offset += round_up(header->header_len, 64);

	// WAD certificates
	ret = Wad_ReadAlloc(fp, (void **)&p_certs, offset, header->certs_len);
	if (ret < 0)
			goto err;
	else
			offset += round_up(header->certs_len, 64);

	// WAD crl
	if (header->crl_len) {
			ret = Wad_ReadAlloc(fp, (void **)&p_crl, offset, header->crl_len);
			if (ret < 0)
					goto err;
			else
					offset += round_up(header->crl_len, 64);
	}

	// WAD ticket
	ret = Wad_ReadAlloc(fp, (void **)&p_tik, offset, header->tik_len);
	if (ret < 0)
			goto err;
	else
			offset += round_up(header->tik_len, 64);

	// WAD TMD
	ret = Wad_ReadAlloc(fp, (void **)&p_tmd, offset, header->tmd_len);
	if (ret < 0)
			goto err;
	else
			offset += round_up(header->tmd_len, 64);
			
	//Patch TMD region to regionfree
	memcpy((p_tmd+103),&region,2);
	
	// Get TMD info
	tmd_data = (tmd *)SIGNATURE_PAYLOAD(p_tmd);
	
		if (tmd_data->title_id == TITLE_ID(1, 1))
	{
		//printf("\n    I can't let you do that Dave\n");
		ret = -999;
		goto out;
	}
	
	if(TITLE_LOWER(tmd_data->sys_version) != NULL && isIOSstub(TITLE_LOWER(tmd_data->sys_version)))
	{
		//printf("\n    This Title wants IOS%i but the installed version\n    is a stub.\n", TITLE_LOWER(tmd_data->sys_version));
		ret = -999;
		goto err;
	}
	
	if(get_title_ios(TITLE_ID(1, 2)) == tmd_data->title_id)
	{
		if ( ( tmd_data->num_contents == 3) && (tmd_data->contents[0].type == 1 && tmd_data->contents[1].type == 0x8001 && tmd_data->contents[2].type == 0x8001) )
		{
			//printf("\n    I won't install a stub System Menu IOS\n");
			ret = -999;
			goto err;
		}
	}
	
	if(tmd_data->title_id  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'E')) || tmd_data->title_id  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'P')) || tmd_data->title_id  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'J')) || tmd_data->title_id  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'K')))
	{
		if ( ( tmd_data->num_contents == 3) && (tmd_data->contents[0].type == 1 && tmd_data->contents[1].type == 0x8001 && tmd_data->contents[2].type == 0x8001) )
		{
			//printf("\n    I won't install a stub EULA IOS\n");
			ret = -999;
			goto err;
		}
	}
	
	if(tmd_data->title_id  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'E')) || tmd_data->title_id  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'P')) || tmd_data->title_id  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'J')) || tmd_data->title_id  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'K')))
	{
		if ( ( tmd_data->num_contents == 3) && (tmd_data->contents[0].type == 1 && tmd_data->contents[1].type == 0x8001 && tmd_data->contents[2].type == 0x8001) )
		{
			//printf("\n    I won't install a stub rgsel IOS\n");
			ret = -999;
			goto err;
		}
	}
	if (tmd_data->title_id == get_title_ios(TITLE_ID(0x10001, 0x48415858)) || tmd_data->title_id == get_title_ios(TITLE_ID(0x10001, 0x4A4F4449)))
	{
		if ( ( tmd_data->num_contents == 3) && (tmd_data->contents[0].type == 1 && tmd_data->contents[1].type == 0x8001 && tmd_data->contents[2].type == 0x8001) )
		{
				ret = -999;
				goto err;
		}
	}
	
	if (tmd_data->title_id == TITLE_ID(1, 2))
	{
		if(get_sm_region_basic() == 0)
		{
			//printf("\n    Can't get the SM region\n    Please check the site for updates\n");
			ret = -999;
			goto err;
		}
		int i, ret = -1;
		for(i = 0; i <= NB_SM; i++)
		{
			if(	regionlist[i].version == tmd_data->title_version)
			{
				ret = 1;
				break;
			}
		}
		
		if(ret -1)
		{
			//printf("\n    Can't get the SM region\n    Please check the site for updates\n");
			ret = -999;
			goto err;
		}
		
		if( get_sm_region_basic() != regionlist[i].region)
		{
			//printf("\n    I won't install the wrong regions SM\n");
			ret = -999;
			goto err;
		}
	}
	
	ResumeGui();
	msg1Txt.SetText("Reading WAD data... Ok!");
	msg2Txt.SetText("Installing ticket...");
	HaltGui();
	// Install ticket
	ret = ES_AddTicket(p_tik, header->tik_len, p_certs, header->certs_len, p_crl, header->crl_len);
	if (ret < 0)
			goto err;

	ResumeGui();
	msg2Txt.SetText("Installing ticket... Ok!");
	msg3Txt.SetText("Installing title...");
	HaltGui();
	// Install title
	ret = ES_AddTitleStart(p_tmd, header->tmd_len, p_certs, header->certs_len, p_crl, header->crl_len);
	if (ret < 0)
			goto err;

	// Install contents
	promptWindow.Append(&progressbarEmptyImg);
	promptWindow.Append(&progressbarImg);
	promptWindow.Append(&progressbarOutlineImg);
	ResumeGui();
	msg3Txt.SetText("Installing title... Ok!");
	for (cnt = 0; cnt < tmd_data->num_contents; cnt++) {
		tmd_content *content = &tmd_data->contents[cnt];

		u32 idx = 0, len;
		s32 cfd;
		ResumeGui();

		// Encrypted content size
		len = round_up(content->size, 64);

		// Install content
		cfd = ES_AddContentStart(tmd_data->title_id, content->cid);
		if (cfd < 0) {
				ret = cfd;
				goto err;
		}
		snprintf(imgPath, sizeof(imgPath), "%s%d...","Installing content ",content->cid);
		msg4Txt.SetText(imgPath);

		// Install content data
		while (idx < len) {
			u32 size;

			// Data length
			size = (len - idx);
			if (size > BLOCK_SIZE)
					size = BLOCK_SIZE;

			// Read data
			ret = Wad_ReadFile(fp, &wadBuffer, offset, size);
			if (ret < 0)
					goto err;

			// Install data
			ret = ES_AddContentData(cfd, wadBuffer, size);
			if (ret < 0)
					goto err;

			// Increase variables
			idx    += size;
			offset += size;
		   
			progressbarImg.SetTile(100*(cnt*len+idx)/(tmd_data->num_contents*len));
		}
		// Finish content installation
		ret = ES_AddContentFinish(cfd);
		if (ret < 0)
				goto err;
	}

	msg4Txt.SetText("Installing content... Ok!");
	msg4Txt.SetText("Finishing installation...");

	// Finish title install
	ret = ES_AddTitleFinish();
	if (ret >= 0) {
//              printf(" OK!\n");
			goto out;
	}

err:
	char temperr[8];
	snprintf(temperr, sizeof(temperr), "%d", ret);
	WindowPrompt("ERROR!",temperr,"Back",0);
	// Cancel install
	ES_AddTitleCancel();
	goto exit;

out:
	// Free memory
	if (header)
			free(header);
	if (p_certs)
			free(p_certs);
	if (p_crl)
			free(p_crl);
	if (p_tik)
			free(p_tik);
	if (p_tmd)
			free(p_tmd);
	goto exit;

exit:
	msg4Txt.SetText("Finishing installation... Ok!");
	
	if (showBtn)
	{
		promptWindow.Append(&btn1);
		while(btn1.GetState() != STATE_CLICKED){
		}
	}

	HaltGui();
	mainWindow->Remove(&promptWindow);
	mainWindow->SetState(STATE_DEFAULT);
	ResumeGui();

	return ret;
}

/****************************************************************************
 * Wad_Uninstall Prompt
 *
 * Window to uninstall WAD and show progress
 ***************************************************************************/

s32 Wad_Uninstall(FILE * fp)
{
	GuiWindow promptWindow(472,320);
	promptWindow.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
    promptWindow.SetPosition(0, -10);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND_PCM);
	GuiSound btnClick(button_click_pcm, button_click_pcm_size, SOUND_PCM);

	GuiImageData btnOutline(button_png);
	GuiImageData dialogBox(dialogue_box_png);
	GuiTrigger trigA;
	trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);

	GuiImage dialogBoxImg(&dialogBox);

	GuiText btn1Txt("Ok", 20, (GXColor){0, 0, 0, 255});
	GuiImage btn1Img(&btnOutline);
	GuiButton btn1(&btn1Img,&btn1Img, 2, 4, 0, -55, &trigA, &btnSoundOver, &btnClick,1);
	btn1.SetLabel(&btn1Txt);
	btn1.SetState(STATE_SELECTED);

	GuiText titleTxt("Uninstalling wad", 24, (GXColor){0, 0, 0, 255});
	titleTxt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	titleTxt.SetPosition(0,40);

	GuiText msg1Txt(NULL, 18, (GXColor){0, 0, 0, 255});
	msg1Txt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	msg1Txt.SetPosition(0,75);

	GuiText msg2Txt(NULL, 18, (GXColor){0, 0, 0, 255});
	msg2Txt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	msg2Txt.SetPosition(0, 98);

	GuiText msg3Txt(NULL, 18, (GXColor){0, 0, 0, 255});
	msg3Txt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	msg3Txt.SetPosition(0, 121);

	GuiText msg4Txt(NULL, 18, (GXColor){0, 0, 0, 255});
	msg4Txt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	msg4Txt.SetPosition(0, 144);

	GuiText msg5Txt(NULL, 18, (GXColor){0, 0, 0, 255});
	msg5Txt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	msg5Txt.SetPosition(0, 167);

	promptWindow.Append(&dialogBoxImg);
	promptWindow.Append(&titleTxt);
	promptWindow.Append(&msg1Txt);
	promptWindow.Append(&msg2Txt);
	promptWindow.Append(&msg3Txt);
	promptWindow.Append(&msg4Txt);
	promptWindow.Append(&msg5Txt);

	HaltGui();
	mainWindow->SetState(STATE_DISABLED);
	mainWindow->Append(&promptWindow);
	mainWindow->ChangeFocus(&promptWindow);
	ResumeGui();

	//WAD
	wadHeader *header   = NULL;
	tikview   *viewData = NULL;

	u64 tid;
	u32 viewCnt;
	s32 ret;

	msg1Txt.SetText("Reading WAD data...");

	// WAD header
	ret = Wad_ReadAlloc(fp, (void **)&header, 0, sizeof(wadHeader));
	if (ret < 0) {
			char errTxt[50];
			sprintf(errTxt,"%sret = %d","Reading WAD data...ERROR! ",ret);
			msg1Txt.SetText(errTxt);
			goto out;
	}
	// Get title ID
	ret =  Wad_GetTitleID(fp, header, &tid);
	if (ret < 0) {
			char errTxt[50];
			sprintf(errTxt,"%sret = %d","Reading WAD data...ERROR! ",ret);
			msg1Txt.SetText(errTxt);
			goto out;
	}
	//Assorted Checks
	if (TITLE_UPPER(tid) == 1 && get_title_ios(TITLE_ID(1, 2)) == 0)
	{
		//printf("\n    I can't determine the System Menus IOS\nDeleting system titles is disabled\n");
		ret = -999;
		goto out;
	}
	if (tid == TITLE_ID(1, 1))
	{
		//printf("\n    I won't try to uninstall boot2\n");
		ret = -999;
		goto out;
	}
	if (tid == TITLE_ID(1, 2))
	{
		//printf("\n    I won't uninstall the System Menu\n");
		ret = -999;
		goto out;
	}
	if(get_title_ios(TITLE_ID(1, 2)) == tid)
	{
		//printf("\n    I won't uninstall the System Menus IOS\n");
		ret = -999;
		goto out;
	}
	if (tid == get_title_ios(TITLE_ID(0x10001, 0x48415858)) || tid == get_title_ios(TITLE_ID(0x10001, 0x4A4F4449)))
	{
		//printf("\n    This is the HBCs IOS, uninstalling will break the HBC!\n");
		//printf("\n    Press A to continue.\n");
		//printf("    Press B skip.");
		
		//u32 buttons = WaitButtons();
		
		//if (!(buttons & WPAD_BUTTON_A))
		//{
			ret = -998;
			goto out;
		//}
	}
	if((tid  == TITLE_ID(0x10008, 0x48414B00 | 'E') || tid  == TITLE_ID(0x10008, 0x48414B00 | 'P') || tid  == TITLE_ID(0x10008, 0x48414B00 | 'J') || tid  == TITLE_ID(0x10008, 0x48414B00 | 'K') 
		|| (tid  == TITLE_ID(0x10008, 0x48414C00 | 'E') || tid  == TITLE_ID(0x10008, 0x48414C00 | 'P') || tid  == TITLE_ID(0x10008, 0x48414C00 | 'J') || tid  == TITLE_ID(0x10008, 0x48414C00 | 'K'))) && get_sm_region_basic() == 0)
	{
		//printf("\n    Can't get the SM region\n    Please check the site for updates\n");
		ret = -999;
		goto out;
	}
	if(tid  == TITLE_ID(0x10008, 0x48414B00 | get_sm_region_basic()))
	{
		//printf("\n    I won't uninstall the EULA\n");
		ret = -999;
		goto out;
	}	
	if(tid  == TITLE_ID(0x10008, 0x48414C00 | get_sm_region_basic()))
	{
		//printf("\n    I won't uninstall rgsel\n");
		ret = -999;
		goto out;
	}	
	if(tid  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | get_sm_region_basic())))
	{
		//printf("\n    I won't uninstall the EULAs IOS\n");
		ret = -999;
		goto out;
	}	
	if(tid  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | get_sm_region_basic())))
	{
		//printf("\n    I won't uninstall the rgsel IOS\n");
		ret = -999;
		goto out;
	}


	msg1Txt.SetText("Reading WAD data...Ok!");
	msg2Txt.SetText("Deleting tickets...");

	// Get ticket views
	ret = Title_GetTicketViews(tid, &viewData, &viewCnt);
	if (ret < 0)
	{
		char errTxt[50];
		sprintf(errTxt,"%sret = %d","Deleting tickets...ERROR! ",ret);
		msg2Txt.SetText(errTxt);
	}
	
	// Delete tickets
	if (ret >= 0) 
	{
		u32 cnt;
		// Delete all tickets
		for (cnt = 0; cnt < viewCnt; cnt++) {
			ret = ES_DeleteTicket(&viewData[cnt]);
			if (ret < 0)
					break;
		}

		if (ret < 0)
		{
			char errTxt[50];
			sprintf(errTxt,"%sret = %d","Deleting tickets...ERROR! ",ret);
			msg2Txt.SetText(errTxt);
		}
		else
			msg2Txt.SetText("Deleting tickets...Ok! ");
	}

	msg3Txt.SetText("Deleting title contents...");

	// Delete title contents
	ret = ES_DeleteTitleContent(tid);
	if (ret < 0){
		char errTxt[50];
		sprintf(errTxt,"%sret = %d","Deleting title contents...ERROR! ",ret);
		msg3Txt.SetText(errTxt);}
	else
		msg3Txt.SetText("Deleting title contents...Ok!");

	msg4Txt.SetText(("Deleting title..."));
	// Delete title
	ret = ES_DeleteTitle(tid);
	if (ret < 0){
		char errTxt[50];
		sprintf(errTxt,"%sret = %d","Deleting title ...ERROR! ",ret);
		msg4Txt.SetText(errTxt);}
	else
		msg4Txt.SetText(("Deleting title ...Ok!"));

out:
	// Free memory
	if (header)
			free(header);

	goto exit;

exit:
	msg5Txt.SetText("Done!");
	promptWindow.Append(&btn1);
	while(btn1.GetState() != STATE_CLICKED){
	}

	HaltGui();
	mainWindow->Remove(&promptWindow);
	mainWindow->SetState(STATE_DEFAULT);
	ResumeGui();

	return ret;
}

std::string queueList[20];
std::string tempcnt;
char * temptxt = NULL;

/****************************************************************************
 * MenuBrowseDevice
 ***************************************************************************/
static int MenuBrowseDevice()
{	
	static char bootDevice[7];
	static s32 selected = 0;
	static int iCnt = 0;
	//static s32 nselected = 1;
	
	s32 ret;
	// Retrieve IOS version 
	/*s32 iosVersion = IOS_GetVersion();
	
	// Disable NAND emulator 
	if (ndev) {
		Nand_Unmount(ndev);
		Nand_Disable();
	}
	
	// Selected device 
	ndev = &ndevList[nselected];
	
	// Mount NAND device 
	ret = Nand_Mount(ndev);
	if (ret < 0) {
		//printf(" ERROR! (ret = %d)\n", ret);
		WindowPrompt("ERROR!", "Nand_Mount","back",0);
	}

	// Enable NAND emulator
	ret = Nand_Enable(ndev);
	if (ret < 0) {
	WindowPrompt("ERROR!", "Nand_Enable","back",0);
		//printf(" ERROR! (ret = %d)\n", ret);
	} else
		WindowPrompt("OK!",ndev->name,"back",0);
		//printf(" OK!\n");
*/

	//* Unmount device
	if (fdev) {
		Unmount_Device(fdev);
		}
	
	// Selected device
	fdev = &fdevList[selected];
	
	Mount_Device(fdev);

	sprintf(bootDevice,"%s:",fdev->mount);
	int i;	
	ShutoffRumble();

	// populate initial directory listing
	if(BrowseDevice(bootDevice) <= 0)
	{
		int choice = WindowPrompt2(
		"Error", "Unable to display files on selected load device.",
		"SD", "SMB", "USB", "Back");

		switch(choice) 
		{
		case 1:
			selected = 0;
			return MENU_BROWSE_DEVICE;
			break;
		case 2:
			selected = 3;
			return MENU_BROWSE_DEVICE;
			break;
		case 3:
			selected = 2;
			return MENU_BROWSE_DEVICE;
			break;
		case 0:
			return MENU_MAIN;
			break;
		}
	}

	int menu = MENU_NONE;

	GuiTrigger trigA;
	trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);
	GuiTrigger trigB;
	trigB.SetButtonOnlyTrigger(-1, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B);

	//Batch stuff
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiText btnbatchTxt("Batch", 20, (GXColor){0, 0, 0, 255});
	GuiImage btnbatchImg(&btnOutline);
	GuiImage btnbatchImgOver(&btnOutlineOver);
	GuiButton btnbatch(btnOutline.GetWidth(), btnOutline.GetHeight());
	btnbatch.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
	btnbatch.SetPosition(-10, 0);
	btnbatch.SetLabel(&btnbatchTxt);
	btnbatch.SetImage(&btnbatchImg);
	btnbatch.SetImageOver(&btnbatchImgOver);
	btnbatch.SetScale(0.8);
	btnbatch.SetTrigger(&trigA);
	btnbatch.SetEffectGrow();
/*****************/
	GuiFileBrowser fileBrowser(450, 390);
	fileBrowser.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	fileBrowser.SetPosition(25, 40);
	
	GuiImageData deviceSd(button_png);
	GuiText deviceSdText("SD Card", 20, (GXColor) {0, 0, 0, 255}); 
	GuiImage deviceSdImg(&deviceSd);
	GuiButton deviceSdBtn(deviceSd.GetWidth(), deviceSd.GetHeight());
	deviceSdBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	deviceSdBtn.SetPosition(-150, fileBrowser.GetTop()-40);
	deviceSdBtn.SetLabel(&deviceSdText);
	deviceSdBtn.SetImage(&deviceSdImg);
	deviceSdBtn.SetTrigger(&trigA);
	deviceSdBtn.SetScale(0.8);
	deviceSdBtn.SetEffectGrow();

	GuiImageData deviceUsb(device_usb_png);
	GuiImage deviceUsbImg(&deviceUsb);
	GuiButton deviceUsbBtn(deviceUsb.GetWidth(), deviceUsb.GetHeight());
	deviceUsbBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	deviceUsbBtn.SetPosition(0, fileBrowser.GetTop()-50);
	deviceUsbBtn.SetImage(&deviceUsbImg);
	deviceUsbBtn.SetTrigger(&trigA);
	deviceUsbBtn.SetScale(0.8);
	deviceUsbBtn.SetEffectGrow();

	GuiImageData deviceSmb(button_png);
	GuiText deviceSmbText("SMB Share", 20, (GXColor) {0, 0, 0, 255}); 
	GuiImage deviceSmbImg(&deviceSmb);
	GuiButton deviceSmbBtn(deviceSmb.GetWidth(), deviceSmb.GetHeight());
	deviceSmbBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	deviceSmbBtn.SetPosition(150, fileBrowser.GetTop()-40);
	deviceSmbBtn.SetLabel(&deviceSmbText);
	deviceSmbBtn.SetImage(&deviceSmbImg);
	deviceSmbBtn.SetTrigger(&trigA);
	deviceSmbBtn.SetScale(0.8);
	deviceSmbBtn.SetEffectGrow();

	GuiText backBtnTxt("Go Back", 20, (GXColor){0, 0, 0, 255});
	GuiImage backBtnImg(&btnOutline);
	GuiImage backBtnImgOver(&btnOutlineOver);
	GuiButton backBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	backBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	backBtn.SetPosition(30, 0);
	backBtn.SetLabel(&backBtnTxt);
	backBtn.SetImage(&backBtnImg);
	backBtn.SetImageOver(&backBtnImgOver);
	backBtn.SetScale(0.8);
	backBtn.SetTrigger(&trigA);
	backBtn.SetTrigger(&trigB);
	backBtn.SetEffectGrow();

	GuiWindow buttonWindow(screenwidth, screenheight);
	buttonWindow.Append(&backBtn);
	buttonWindow.Append(&deviceSdBtn);
	buttonWindow.Append(&deviceUsbBtn);
	buttonWindow.Append(&deviceSmbBtn);
	
	buttonWindow.Append(&btnbatch);
	
	HaltGui();
	mainWindow->Append(&fileBrowser);
	mainWindow->Append(&buttonWindow);
	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(THREAD_SLEEP);

		// update file browser based on arrow buttons
		// set MENU_EXIT if A button pressed on a file
		for(i=0; i < FILE_PAGESIZE; i++)
		{
			if(fileBrowser.fileList[i]->GetState() == STATE_CLICKED)
			{
				fileBrowser.fileList[i]->ResetState();
				// check corresponding browser entry
				if(browserList[browser.selIndex].isdir)
				{
					if(BrowserChangeFolder())
					{
						fileBrowser.ResetState();
						fileBrowser.fileList[0]->SetState(STATE_SELECTED);
						fileBrowser.TriggerUpdate();
					}
					else
					{
						menu = MENU_BROWSE_DEVICE;
						break;
					}
				}
				else
				{
					mainWindow->SetState(STATE_DISABLED);
					//Check for WAD filename extension
					if(checkExt(browserList[browser.selIndex].filename,"wad") || checkExt(browserList[browser.selIndex].filename,"WAD")){
					int x = 0, y = 0;
					x = userInput[0].wpad.ir.x;
					y = userInput[0].wpad.ir.y;
					
					
					// load file
					char temp[MAXPATHLEN];
					sprintf(temp,"%s/%s",cfulldir,browserList[browser.selIndex].filename);
					char ID[5];
					char Region[5];
					Wad_GetID(temp,ID);
					Wad_GetRegion(ID,Region);
										
					//Open PopUpMenu
					s32 ret = WAD_PopUpMenu(x,y);
					
					FILE *fp;
					switch(ret)
					{
					case 0: //Install WAD
						fp = fopen(temp,"rb");
						
						if (fp != NULL)
							Wad_Install(fp,true);
						
						fclose(fp);
						break;
					case 1: //Uninstall WAD
						fp = fopen(temp,"rb");
						
						if (fp != NULL)
							Wad_Uninstall(fp);
							
						fclose(fp);
						break;
					case 2:  //Add to download queue
						ret = WindowPrompt("Add to Batchlist",temp,"Yes","No");
						if (ret == 1)
							{
							if (iCnt < 20) {
								queueList[iCnt] = temp;
								iCnt = iCnt + 1;
								}
								else WindowPrompt("Batchlist is full",NULL,"Back",NULL);
							}
						break;
					case 3:  //Show WAD Infos todo
						temptxt = (char*) malloc(40);
						snprintf(temptxt,28,"GameID: %sRegion: %s",ID,Region);
						WindowPrompt(browserList[browser.selIndex].filename,temptxt,"Back",NULL);
						break;
					default:
						break;
					}
					
					}
					mainWindow->SetState(STATE_DEFAULT);
				}
			}
		}
		
			//Batch stuff
			if(btnbatch.GetState() == STATE_CLICKED)
				{
				
				if (iCnt != 0) {
				
					char cCnt[5];
					sprintf(cCnt,"%i",iCnt);
					int ret = WindowPrompt("Install Count Games ?",cCnt,"Okay !","Nope");		
					
					if (ret == 1) {
						int x = 0;
						while( x < iCnt)
						{
						FILE *fp;
						fp = fopen(queueList[x].c_str(),"rb");
								
						if (fp != NULL)
							Wad_Install(fp,false);
								
						fclose(fp);
						x++;
						}
						
						WindowPrompt("Installed Games",NULL,"Okay !",NULL);
						iCnt = 0;
					}
				}
				else WindowPrompt("Batchlist empty",NULL,"Back",NULL);
				
				
				btnbatch.ResetState();
				
			}
				
		
		if(backBtn.GetState() == STATE_CLICKED)
		{
			iCnt = 0;
			menu = MENU_MAIN;
		}
		else if (deviceSdBtn.GetState() == STATE_CLICKED)
		{
		selected = 0;
		menu = MENU_BROWSE_DEVICE;
		}
		else if (deviceUsbBtn.GetState() == STATE_CLICKED)
		{
		selected = 2;
		menu = MENU_BROWSE_DEVICE;
		}
		else if (deviceSmbBtn.GetState() == STATE_CLICKED)
		{
		selected = 3;
		menu = MENU_BROWSE_DEVICE;
		}
	}

	HaltGui();
	mainWindow->Remove(&buttonWindow);
	mainWindow->Remove(&fileBrowser);
	return menu;
}

/*static int MenuChannelLoader()
{
int menu = MENU_NONE;

return menu;
}*/

/****************************************************************************
 * MenuMain
 ***************************************************************************/
static int MenuMain()
{
	int menu = MENU_NONE;
	
	GuiText titleTxt("WAD Manager 1.5", 24, (GXColor){0, 0, 0, 255});
	titleTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	titleTxt.SetPosition(50,50);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND_PCM);
	GuiImageData btnLogo(logo_png);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiImageData btnLargeOutline(button_png);
	GuiImageData btnLargeOutlineOver(button_over_png);

	GuiTrigger trigA;
	trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);
	GuiTrigger trigHome;
	trigHome.SetButtonOnlyTrigger(-1, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, 0);

	GuiImage logoBtnImg(&btnLogo);
	GuiButton logoBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	logoBtn.SetAlignment(ALIGN_CENTRE, ALIGN_BOTTOM);
	logoBtn.SetPosition(200, -72);
	logoBtn.SetImage(&logoBtnImg);
	logoBtn.SetSoundOver(&btnSoundOver);
	logoBtn.SetTrigger(&trigA);
	logoBtn.SetEffectGrow();

	GuiText fileBtnTxt("Browser", 20, (GXColor){0, 0, 0, 255});
	GuiImage fileBtnImg(&btnLargeOutline);
	GuiImage fileBtnImgOver(&btnLargeOutlineOver);
	GuiButton fileBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	fileBtn.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	fileBtn.SetPosition(50, 120);
	fileBtn.SetLabel(&fileBtnTxt);
	fileBtn.SetImage(&fileBtnImg);
	fileBtn.SetImageOver(&fileBtnImgOver);
	fileBtn.SetSoundOver(&btnSoundOver);
	fileBtn.SetTrigger(&trigA);
	fileBtn.SetEffectGrow();

	GuiText smbSettingsBtnTxt("SMB Settings", 20, (GXColor){0, 0, 0, 255});
	GuiImage smbSettingsBtnImg(&btnLargeOutline);
	GuiImage smbSettingsBtnImgOver(&btnLargeOutlineOver);
	GuiButton smbSettingsBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	smbSettingsBtn.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	smbSettingsBtn.SetPosition(50, 180);
	smbSettingsBtn.SetLabel(&smbSettingsBtnTxt);
	smbSettingsBtn.SetImage(&smbSettingsBtnImg);
	smbSettingsBtn.SetImageOver(&smbSettingsBtnImgOver);
	smbSettingsBtn.SetSoundOver(&btnSoundOver);
	smbSettingsBtn.SetTrigger(&trigA);
	smbSettingsBtn.SetEffectGrow();
	
	GuiText SettingsBtnTxt("Settings", 20, (GXColor){0, 0, 0, 255});
	GuiImage SettingsBtnImg(&btnLargeOutline);
	GuiImage SettingsBtnImgOver(&btnLargeOutlineOver);
	GuiButton SettingsBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	SettingsBtn.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	SettingsBtn.SetPosition(50, 240);
	SettingsBtn.SetLabel(&SettingsBtnTxt);
	SettingsBtn.SetImage(&SettingsBtnImg);
	SettingsBtn.SetImageOver(&SettingsBtnImgOver);
	SettingsBtn.SetSoundOver(&btnSoundOver);
	SettingsBtn.SetTrigger(&trigA);
	SettingsBtn.SetEffectGrow();

	GuiText exitBtnTxt("Exit", 20, (GXColor){0, 0, 0, 255});
	GuiImage exitBtnImg(&btnOutline);
	GuiImage exitBtnImgOver(&btnOutlineOver);
	GuiButton exitBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	exitBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	exitBtn.SetPosition(100, -35);
	exitBtn.SetLabel(&exitBtnTxt);
	exitBtn.SetImage(&exitBtnImg);
	exitBtn.SetImageOver(&exitBtnImgOver);
	exitBtn.SetSoundOver(&btnSoundOver);
	exitBtn.SetTrigger(&trigA);
	exitBtn.SetTrigger(&trigHome);
	exitBtn.SetScale(0.8);
	exitBtn.SetEffectGrow();

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&fileBtn);
	w.Append(&smbSettingsBtn);
	w.Append(&SettingsBtn);
	w.Append(&exitBtn);
	//w.Append(&logoBtn);
	mainWindow->Append(&w);

	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(THREAD_SLEEP);

		if(fileBtn.GetState() == STATE_CLICKED)
		{
			menu = MENU_BROWSE_DEVICE;
		}
		else if(smbSettingsBtn.GetState() == STATE_CLICKED)
		{
			menu = MENU_SMB_SETTINGS;
		}
			else if(SettingsBtn.GetState() == STATE_CLICKED)
		{
			menu = MENU_SETTINGS;
		}
		else if(exitBtn.GetState() == STATE_CLICKED)
		{	
			//Sys_BackToLoader();
			int choice = WindowPrompt2(
				NULL,
				NULL,
				"HBC",
				"Wii Menu",
				"Back",NULL);

			if(choice == 1)
			{
			menu = MENU_EXIT;				
			}
			else if(choice == 2)
			{
			Sys_LoadMenu();
			}
			else if(choice == 3)
			{
			menu = MENU_MAIN;
			}
			
		}
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

/****************************************************************************
 * MenuSettingsFile
 ***************************************************************************/

static int MenuSettingsFile()
{
	CloseSMB();
	DefaultSMBSettings();
	int menu = MENU_NONE;
	int ret;
	int i = 0;
	OptionList options;
	sprintf(options.name[i++], "User");
	sprintf(options.name[i++], "Password");
	sprintf(options.name[i++], "Share Name");
	sprintf(options.name[i++], "Share IP");
	options.length = i;

	GuiText titleTxt("SMB Settings", 24, (GXColor){0, 0, 0, 255});
	titleTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	titleTxt.SetPosition(50,50);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND_PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);

	GuiTrigger trigA;
	trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);
	GuiTrigger trigB;
	trigB.SetButtonOnlyTrigger(-1, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B);

	GuiText backBtnTxt("Go Back", 20, (GXColor){0, 0, 0, 255});
	GuiImage backBtnImg(&btnOutline);
	GuiImage backBtnImgOver(&btnOutlineOver);
	GuiButton backBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	backBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	backBtn.SetPosition(100, -35);
	backBtn.SetLabel(&backBtnTxt);
	backBtn.SetImage(&backBtnImg);
	backBtn.SetImageOver(&backBtnImgOver);
	backBtn.SetSoundOver(&btnSoundOver);
	backBtn.SetTrigger(&trigA);
	backBtn.SetTrigger(&trigB);
	backBtn.SetEffectGrow();

	GuiText saveBtnTxt("Save", 20, (GXColor){0, 0, 0, 255});
	GuiImage saveBtnImg(&btnOutline);
	GuiImage saveBtnImgOver(&btnOutlineOver);
	GuiButton saveBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	saveBtn.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
	saveBtn.SetPosition(-100, -35);
	saveBtn.SetLabel(&saveBtnTxt);
	saveBtn.SetImage(&saveBtnImg);
	saveBtn.SetImageOver(&saveBtnImgOver);
	saveBtn.SetSoundOver(&btnSoundOver);
	saveBtn.SetTrigger(&trigA);
	saveBtn.SetEffectGrow();

	GuiOptionBrowser optionBrowser(552, 248, &options);
	optionBrowser.SetPosition(0, 108);
	optionBrowser.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	optionBrowser.SetCol2Position(185);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&backBtn);
	w.Append(&saveBtn);
	
	mainWindow->Append(&optionBrowser);
	mainWindow->Append(&w);
	mainWindow->Append(&titleTxt);
	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(THREAD_SLEEP);

		snprintf (options.value[0], 20, "%s", SMBSettings.SMB_USER);
		snprintf (options.value[1], 40, "%s", SMBSettings.SMB_PWD);
		snprintf (options.value[2], 40, "%s", SMBSettings.SMB_SHARE);
		snprintf (options.value[3], 20, "%s", SMBSettings.SMB_IP);

		ret = optionBrowser.GetClickedOption();

		switch (ret)
		{
			case 0:
				OnScreenKeyboard(SMBSettings.SMB_USER, 20);
				break;
			case 1:
				OnScreenKeyboard(SMBSettings.SMB_PWD, 40);
				break;
			case 2:
				OnScreenKeyboard(SMBSettings.SMB_SHARE, 40);
				break;
			case 3:
				OnScreenKeyboard(SMBSettings.SMB_IP, 20);				
				break;
	}

		if(backBtn.GetState() == STATE_CLICKED)
		{
			menu = MENU_MAIN;
		}
		
		if(saveBtn.GetState() == STATE_CLICKED)
		{
			fatInitDefault();
			//char tempPath[100];
			
			//sprintf(tempPath,"%sconfig.xml",appPath);
			CreateXmlFile(xmlPath, &SMBSettings);
			WindowPrompt("Saved Settings",xmlPath,"OK",NULL);
			menu = MENU_SMB_SETTINGS;
		}
	}
	HaltGui();
	mainWindow->Remove(&optionBrowser);
	mainWindow->Remove(&w);
	mainWindow->Remove(&titleTxt);
	return menu;
}

/****************************************************************************
 * MainMenu
 ***************************************************************************/
void MainMenu(int menu)
{	
	int currentMenu = menu;

	#ifdef HW_RVL
	pointer[0] = new GuiImageData(player1_point_png);
	pointer[1] = new GuiImageData(player2_point_png);
	#endif

	mainWindow = new GuiWindow(screenwidth, screenheight);

	GuiImageData bg(bg_png);
	bgImg = new GuiImage(&bg);
	mainWindow->Append(bgImg);

	GuiTrigger trigA;
	trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);

	ResumeGui();

	
	while(currentMenu != MENU_EXIT)
	{
		switch (currentMenu)
		{
			case MENU_MAIN:
				currentMenu = MenuMain();
				break;
			case MENU_SMB_SETTINGS:
				currentMenu = MenuSettingsFile();//MenuChannelLoader();
				break;
			//case MENU_SETTINGS:
				//currentMenu = MenuSettings();//MenuChannelLoader();
				//break;
			case MENU_BROWSE_DEVICE:
				currentMenu = MenuBrowseDevice();
				break;
			default: 
				currentMenu = MenuMain();
				break;
		}
	}

	// Disable NAND emulator
	//Nand_Disable();
	
	ResumeGui();
	ExitRequested = 1;
	while(1) usleep(THREAD_SLEEP);

	HaltGui();

	delete bgImg;
	delete mainWindow;

	delete pointer[0];
	delete pointer[1];

	mainWindow = NULL;
}
