/****************************************************************************
 * Samba Operations
 *
 ****************************************************************************/

#include <stdio.h>

#include <ogcsys.h>
#include <ogc/machine/processor.h>
#include <smb.h>

#include "demo.h"
#include "http.h"
#include "smb.h"

static lwp_t networkthread = LWP_THREAD_NULL;
static bool networkHalt = true;

char * WII_IP;

static int connected = 0;
static int netinited = 0;
unsigned int SMBTimer = 0;
#define SMBTIMEOUT (3600)	/*** Some implementations timeout in 10 minutes ***/

/****************************************************************************
 * Initialize_Network
 ***************************************************************************/
void initNetwork(void) {

  if (netinited == 0)
    {
    s32 result;
    result = if_config(WII_IP, NULL, NULL, true);

    if (result < 0) {
        netinited = 0;
		} else {
        netinited = 1;
		}
	}
}

/****************************************************************************
 * Connect to SMB
 ***************************************************************************/
bool ConnectSMB ()
{
    if (SMBTimer > SMBTIMEOUT)
    {
        connected = 0;
        SMBTimer = 0;
    }
    
    if (connected == 0)
    {
		initNetwork();
		if (smbInitDevice("smb",SMBSettings.SMB_USER,SMBSettings.SMB_PWD,SMBSettings.SMB_SHARE,SMBSettings.SMB_IP))
        {
			connected = 0;
			return false;
        }
       	else
		{
            connected = 1;
			return true;
		}
    }  
	
	return true;
}

void CloseSMB()
{
	if(connected)
		smbClose("smb");
		
	connected = 0;
	netinited = 0; // trigger a network reinit
}

void SMB_Reconnect()
{
        if(connected)
                smbCheckConnection("smb");
    else {
        if(smbInitDevice("smb",
			SMBSettings.SMB_USER,
			SMBSettings.SMB_PWD,
			SMBSettings.SMB_SHARE,
			SMBSettings.SMB_IP))
        {
            connected = true;
        } else {
            connected = false;
        }
    }
}

/****************************************************************************
 * ResumeNetworkThread
 ***************************************************************************/
void ResumeNetworkThread() {
    networkHalt = false;
    LWP_ResumeThread(networkthread);
}

/*********************************************************************************
 * Networkthread for background network initialize and update check with idle prio
 *********************************************************************************/
static void * netinitcallback(void *arg) {
    while (1) {

        if (!networkHalt)
            LWP_SuspendThread(networkthread);

        initNetwork();

        if (netinited == 1) {

            //suspend thread
            networkHalt = true;
        }
    }
    return NULL;
}

/****************************************************************************
 * InitNetworkThread with priority 0 (idle)
 ***************************************************************************/
void InitNetworkThread() {
    LWP_CreateThread (&networkthread, netinitcallback, NULL, NULL, 0, 0);
}

/****************************************************************************
 * ShutdownThread
 ***************************************************************************/
void ShutdownNetworkThread() {
    LWP_JoinThread (networkthread, NULL);
    networkthread = LWP_THREAD_NULL;
}
