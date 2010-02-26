#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <ogcsys.h>
#include <ogc/pad.h>

#include "sys.h"
#include "title.h"
#include "utils.h"
#include "video.h"
#include "wad.h"
#include "wpad.h"

// Turn upper and lower into a full title ID
#define TITLE_ID(x,y)		(((u64)(x) << 32) | (y))
// Get upper or lower half of a title ID
#define TITLE_UPPER(x)		((u32)((x) >> 32))
// Turn upper and lower into a full title ID
#define TITLE_LOWER(x)		((u32)(x))

u32 WaitButtons(void);
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
			tmd *t = SIGNATURE_PAYLOAD(s_tmd);
			return t->sys_version;
		}
		
	} 
	return 0;
}

/* 'WAD Header' structure */
typedef struct {
	/* Header length */
	u32 header_len;

	/* WAD type */
	u16 type;

	u16 padding;

	/* Data length */
	u32 certs_len;
	u32 crl_len;
	u32 tik_len;
	u32 tmd_len;
	u32 data_len;
	u32 footer_len;
} ATTRIBUTE_PACKED wadHeader;

/* Variables */
static u8 wadBuffer[BLOCK_SIZE] ATTRIBUTE_ALIGN(32);


s32 __Wad_ReadFile(FILE *fp, void *outbuf, u32 offset, u32 len)
{
	s32 ret;

	/* Seek to offset */
	fseek(fp, offset, SEEK_SET);

	/* Read data */
	ret = fread(outbuf, len, 1, fp);
	if (ret < 0)
		return ret;

	return 0;
}

s32 __Wad_ReadAlloc(FILE *fp, void **outbuf, u32 offset, u32 len)
{
	void *buffer = NULL;
	s32   ret;

	/* Allocate memory */
	buffer = memalign(32, len);
	if (!buffer)
		return -1;

	/* Read file */
	ret = __Wad_ReadFile(fp, buffer, offset, len);
	if (ret < 0) {
		free(buffer);
		return ret;
	}

	/* Set pointer */
	*outbuf = buffer;

	return 0;
}

s32 __Wad_GetTitleID(FILE *fp, wadHeader *header, u64 *tid)
{
	signed_blob *p_tik    = NULL;
	tik         *tik_data = NULL;

	u32 offset = 0;
	s32 ret;

	/* Ticket offset */
	offset += round_up(header->header_len, 64);
	offset += round_up(header->certs_len,  64);
	offset += round_up(header->crl_len,    64);

	/* Read ticket */
	ret = __Wad_ReadAlloc(fp, (void *)&p_tik, offset, header->tik_len);
	if (ret < 0)
		goto out;

	/* Ticket data */
	tik_data = (tik *)SIGNATURE_PAYLOAD(p_tik);

	/* Copy title ID */
	*tid = tik_data->titleid;

out:
	/* Free memory */
	if (p_tik)
		free(p_tik);

	return ret;
}


s32 Wad_Install(FILE *fp)
{
	wadHeader   *header  = NULL;
	signed_blob *p_certs = NULL, *p_crl = NULL, *p_tik = NULL, *p_tmd = NULL;

	tmd *tmd_data  = NULL;

	u32 cnt, offset = 0;
	s32 ret;
	u64 tid;

	printf("\t\t>> Reading WAD data...");
	fflush(stdout);

	/* WAD header */
	ret = __Wad_ReadAlloc(fp, (void *)&header, offset, sizeof(wadHeader));
	if (ret < 0)
		goto err;
	else
		offset += round_up(header->header_len, 64);
	//Don't try to install boot2
	__Wad_GetTitleID(fp, header, &tid);
	if (tid == TITLE_ID(1, 1))
	{
		printf("\n    I can't let you do that Dave\n");
		ret = -1;
		goto out;
	}
	if (tid == TITLE_ID(1, 2))
	{
		printf("\n    This is the System Menu, installing the wrong regions SM\n    will break your Wii");
		printf("\n    Press A to continue.\n");
		printf("    Press B skip.");
		
		u32 buttons = WaitButtons();
		
		if (!(buttons & WPAD_BUTTON_A))
		{
			ret = -1;
			goto out;
		}
	}
	/* WAD certificates */
	ret = __Wad_ReadAlloc(fp, (void *)&p_certs, offset, header->certs_len);
	if (ret < 0)
		goto err;
	else
		offset += round_up(header->certs_len, 64);

	/* WAD crl */
	if (header->crl_len) {
		ret = __Wad_ReadAlloc(fp, (void *)&p_crl, offset, header->crl_len);
		if (ret < 0)
			goto err;
		else
			offset += round_up(header->crl_len, 64);
	}

	/* WAD ticket */
	ret = __Wad_ReadAlloc(fp, (void *)&p_tik, offset, header->tik_len);
	if (ret < 0)
		goto err;
	else
		offset += round_up(header->tik_len, 64);

	/* WAD TMD */
	ret = __Wad_ReadAlloc(fp, (void *)&p_tmd, offset, header->tmd_len);
	if (ret < 0)
		goto err;
	else
		offset += round_up(header->tmd_len, 64);

	Con_ClearLine();

	printf("\t\t>> Installing ticket...");
	fflush(stdout);

	/* Install ticket */
	ret = ES_AddTicket(p_tik, header->tik_len, p_certs, header->certs_len, p_crl, header->crl_len);
	if (ret < 0)
		goto err;

	Con_ClearLine();

	printf("\r\t\t>> Installing title...");
	fflush(stdout);

	/* Install title */
	ret = ES_AddTitleStart(p_tmd, header->tmd_len, p_certs, header->certs_len, p_crl, header->crl_len);
	if (ret < 0)
		goto err;

	/* Get TMD info */
	tmd_data = (tmd *)SIGNATURE_PAYLOAD(p_tmd);
	if(isIOSstub(TITLE_LOWER(tmd_data->sys_version)))
	{
		printf("\n    This Title wants IOS%i but the installed version\n    is a stub.\n", TITLE_LOWER(tmd_data->sys_version));
		ret = -1;
		goto err;
	}

	/* Install contents */
	for (cnt = 0; cnt < tmd_data->num_contents; cnt++) {
		tmd_content *content = &tmd_data->contents[cnt];

		u32 idx = 0, len;
		s32 cfd;

		Con_ClearLine();

		printf("\r\t\t>> Installing content #%02d...", content->cid);
		fflush(stdout);

		/* Encrypted content size */
		len = round_up(content->size, 64);

		/* Install content */
		cfd = ES_AddContentStart(tmd_data->title_id, content->cid);
		if (cfd < 0) {
			ret = cfd;
			goto err;
		}

		/* Install content data */
		while (idx < len) {
			u32 size;

			/* Data length */
			size = (len - idx);
			if (size > BLOCK_SIZE)
				size = BLOCK_SIZE;

			/* Read data */
			ret = __Wad_ReadFile(fp, &wadBuffer, offset, size);
			if (ret < 0)
				goto err;

			/* Install data */
			ret = ES_AddContentData(cfd, wadBuffer, size);
			if (ret < 0)
				goto err;

			/* Increase variables */
			idx    += size;
			offset += size;
		}

		/* Finish content installation */
		ret = ES_AddContentFinish(cfd);
		if (ret < 0)
			goto err;
	}

	Con_ClearLine();

	printf("\r\t\t>> Finishing installation...");
	fflush(stdout);

	/* Finish title install */
	ret = ES_AddTitleFinish();
	if (ret >= 0) {
		printf(" OK!\n");
		goto out;
	}

err:
	printf(" ERROR! (ret = %d)\n", ret);

	/* Cancel install */
	ES_AddTitleCancel();

out:
	/* Free memory */
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

	return ret;
}

s32 Wad_Uninstall(FILE *fp)
{
	wadHeader *header   = NULL;
	tikview   *viewData = NULL;

	u64 tid;
	u32 viewCnt;
	s32 ret;

	printf("\t\t>> Reading WAD data...");
	fflush(stdout);

	/* WAD header */
	ret = __Wad_ReadAlloc(fp, (void *)&header, 0, sizeof(wadHeader));
	if (ret < 0) {
		printf(" ERROR! (ret = %d)\n", ret);
		goto out;
	}

	/* Get title ID */
	ret =  __Wad_GetTitleID(fp, header, &tid);
	if (ret < 0) {
		printf(" ERROR! (ret = %d)\n", ret);
		goto out;
	}
	//Assorted Checks
	if (TITLE_UPPER(tid) == 1 && get_title_ios(TITLE_ID(1, 2)) == 0)
	{
		printf("\n    I can't determine the System Menus IOS\nDeleting system titles is disabled\n");
		ret = -1;
		goto out;
	}
	if (tid == TITLE_ID(1, 1))
	{
		printf("\n    I won't try to uninstall boot2\n");
		ret = -1;
		goto out;
	}
	if (tid == TITLE_ID(1, 2))
	{
		printf("\n    I won't uninstall the System Menu\n");
		ret = -1;
		goto out;
	}
	if(get_title_ios(TITLE_ID(1, 2)) == tid)
	{
		printf("\n    I won't uninstall the System Menus IOS\n");
		ret = -1;
		goto out;
	}
	if(tid  == TITLE_ID(0x10008, 0x48414B00 | 'E') || tid  == TITLE_ID(0x10008, 0x48414B00 | 'P') || tid  == TITLE_ID(0x10008, 0x48414B00 | 'J') || tid  == TITLE_ID(0x10008, 0x48414B00 | 'K'))
	{
		printf("\n    I won't uninstall the EULA\n");
		ret = -1;
		goto out;
	}	
	if(tid  == TITLE_ID(0x10008, 0x48414C00 | 'E') || tid  == TITLE_ID(0x10008, 0x48414C00 | 'P') || tid  == TITLE_ID(0x10008, 0x48414C00 | 'J') || tid  == TITLE_ID(0x10008, 0x48414C00 | 'K'))
	{
		printf("\n    I won't uninstall rgsel\n");
		ret = -1;
		goto out;
	}	
	if(tid  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'E')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'P')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'J')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414B00 | 'K')))
	{
		printf("\n    I won't uninstall the EULAs IOS\n");
		ret = -1;
		goto out;
	}	
	if(tid  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'E')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'P')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'J')) || tid  == get_title_ios(TITLE_ID(0x10008, 0x48414C00 | 'K')))
	{
		printf("\n    I won't uninstall the rgsel IOS\n");
		ret = -1;
		goto out;
	}
	if (tid == get_title_ios(TITLE_ID(0x10001, 0x48415858)) || tid == get_title_ios(TITLE_ID(0x10001, 0x4A4F4449)))
	{
		printf("\n    This is the HBCs IOS, uninstalling will break the HBC!\n");
		printf("\n    Press A to continue.\n");
		printf("    Press B skip.");
		
		u32 buttons = WaitButtons();
		
		if (!(buttons & WPAD_BUTTON_A))
			ret = -1;
			goto out;
	}

	Con_ClearLine();

	printf("\t\t>> Deleting tickets...");
	fflush(stdout);

	/* Get ticket views */
	ret = Title_GetTicketViews(tid, &viewData, &viewCnt);
	if (ret < 0)
		printf(" ERROR! (ret = %d)\n", ret);

	/* Delete tickets */
	if (ret >= 0) {
		u32 cnt;

		/* Delete all tickets */
		for (cnt = 0; cnt < viewCnt; cnt++) {
			ret = ES_DeleteTicket(&viewData[cnt]);
			if (ret < 0)
				break;
		}

		if (ret < 0)
			printf(" ERROR! (ret = %d\n", ret);
		else
			printf(" OK!\n");
	}

	printf("\t\t>> Deleting title contents...");
	fflush(stdout);

	/* Delete title contents */
	ret = ES_DeleteTitleContent(tid);
	if (ret < 0)
		printf(" ERROR! (ret = %d)\n", ret);
	else
		printf(" OK!\n");


	printf("\t\t>> Deleting title...");
	fflush(stdout);

	/* Delete title */
	ret = ES_DeleteTitle(tid);
	if (ret < 0)
		printf(" ERROR! (ret = %d)\n", ret);
	else
		printf(" OK!\n");

out:
	/* Free memory */
	if (header)
		free(header);

	return ret;
}
