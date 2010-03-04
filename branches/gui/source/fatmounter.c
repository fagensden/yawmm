#include <fat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
//#include <ogc/lwp_watchdog.h>
//#include <ogc/mutex.h>
#include <ogc/system.h>
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>
#include <ntfs.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statvfs.h>
#include "usbstorage.h"
#include "fatmounter.h"

//these are the only stable and speed is good
#define CACHE 8
#define SECTORS 64

s32 Mount_Device(fatDevice *dev)
{
	s32 ret;
	
	if(dev->mount[0] == 's' && dev->mount[1] == 'm')
		return 0;

	/* Initialize interface */
	ret = dev->interface->startup();
	if (!ret)
		return -1;

	/* Mount device */
	ret = fatMountSimple(dev->mount, dev->interface);
	//ret = fatMount(dev->mount, dev->interface, 0, CACHE, SECTORS);
	if (!ret)
		return -1;

	return 0;
}

void Unmount_Device(fatDevice *dev)
{
	/* Unmount device */
	fatUnmount(dev->mount);

	/* Shutdown interface */
	dev->interface->shutdown();
}

int isSdInserted() {
    return __io_wiisd.isInserted();
}

static int mountCount = 0;
static ntfs_md *mounts = NULL;

int NTFS_Init() {
	fatInitDefault();
	// Mount all NTFS volumes on all inserted block devices
    mountCount = ntfsMountAll(&mounts, NTFS_DEFAULT | NTFS_RECOVER);
    if (mountCount == -1)
        return -2;//printf("Error whilst mounting devices (%i).\n", errno);
    else if (mountCount > 0)
        return 0;//printf("%i NTFS volumes(s) mounted!\n\n", mountCount);
    else
        return -1;//printf("No NTFS volumes were found and/or mounted.\n");
}

void NTFS_deInit() {
	int i = 0;
  // Unmount all NTFS volumes and clean up
    if (mounts) {
        for (i = 0; i < mountCount; i++)
            ntfsUnmount(mounts[i].name, true); 
        free(mounts);
    }
}

void Device_Size(const char * device,long * pdisk,
				long * pfree,long * pused) 
{  
	//char *filename = "USB:/";
	struct statvfs buf;
	if (!statvfs(device, &buf)) 
	{
		long blksize, blocks, freeblks, disk_size, used, free;

		blksize = buf.f_bsize;
		blocks = buf.f_blocks;
		freeblks = buf.f_bfree;

		disk_size = blocks * blksize;
		free = freeblks * blksize;
		used = disk_size - free;
		*pfree = free;
		*pdisk = disk_size;
		*pfree = free;
		*pused = used;
		//printf("Disk usage : %lu \t Free space %lu\n", used, free);
	} 
	else 
	{
		printf("Couldn't get device sizes\n");
	}
}
