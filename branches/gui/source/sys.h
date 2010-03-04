#ifndef _SYS_H_
#define _SYS_H_

/* Prototypes */
void Sys_Init(void);
void Sys_Reboot(void);
void Sys_Shutdown(void);
void Sys_ShutdownToIdel(void);
void Sys_ShutdownToStandby(void);
void Sys_LoadMenu(void);
void Sys_BackToLoader(void);
bool isIOSstub(u8 ios_number);
bool loadIOS(int IOS);

#endif
