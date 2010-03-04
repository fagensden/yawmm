/****************************************************************************
 * Samba Operations
 *
 * Written by dhewg/bushing 
 * modified by nIxx
 ****************************************************************************/
 
#ifndef _SMB_H_
#define _SMB_H_

bool ConnectSMB();
void initNetwork(void);
bool ShutdownWC24();
void CloseSMB();

void HaltNetworkThread();
void ResumeNetworkWait();
void ResumeNetworkThread();
void InitNetworkThread();
void ShutdownNetworkThread();

#endif
