#ifndef _FATMOUNTER_H_
#define _FATMOUNTER_H_

#ifdef __cplusplus
extern "C" {
#endif

/* 'FAT Device' structure */
typedef struct {
	/* Device mount point */
	const char *mount;

	/* Device name */
	const char *name;

	/* Device interface */
	const DISC_INTERFACE *interface;
} fatDevice;

s32 Mount_Device(fatDevice *dev);
void Unmount_Device(fatDevice *dev);
s32 isSdInserted();
s32 isInserted(const char *path);
//void Device_Size(const char * device,unsigned long * pdisk,unsigned long * pfree,unsigned long * pused);

#ifdef __cplusplus
}
#endif

#endif
