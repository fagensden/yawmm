/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * menu.h
 * Menu flow routines - handles all menu logic
 ***************************************************************************/

#ifndef _MENU_H_
#define _MENU_H_

#include <ogcsys.h>

extern struct titleList tList;

void InitGUIThreads();
void MainMenu (int menuitem);
int WindowPrompt(const char *title, const char *msg, const char *btn1Label, const char *btn2Label);

enum
{
	MENU_EXIT = -1,
	MENU_NONE,
	MENU_MAIN,
	MENU_SMB_SETTINGS,
	MENU_SETTINGS,
	MENU_BROWSE_DEVICE
};

enum
{
    INSTALL,
    UNINSTALL,
    ADDBATCH,
    PROPERTIES
};

#endif
