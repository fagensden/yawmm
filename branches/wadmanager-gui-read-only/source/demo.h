/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * demo.h
 ***************************************************************************/

#ifndef _DEMO_H_
#define _DEMO_H_

#include "FreeTypeGX.h"

struct SSettingsSMB {
    char WII_IP[20];	/*** IP to assign the GameCube ***/
	char GW_IP[20];	/*** Your gateway IP ***/
	char MASK[20]; 	/*** Your subnet mask ***/
	char SMB_USER[64];	/*** Your share user ***/
	char SMB_PWD[64];	/*** Your share user password ***/
	char SMB_GCID[128];	/*** Machine Name of GameCube ***/
	char SMB_SVID[128];	/*** Machine Name of Server(Share) ***/
	char SMB_SHARE[64];	/*** Share name on server ***/
	char SMB_IP[20];	/*** IP Address of share server ***/
};

extern struct SSettingsSMB SMBSettings;
extern char appPath[70];
extern char xmlPath[80];

void ExitApp();
extern int ExitRequested;
extern FreeTypeGX *fontSystem[];
void DefaultSMBSettings();
#endif
