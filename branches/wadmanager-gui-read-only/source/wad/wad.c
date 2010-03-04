#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <ogcsys.h>
#include <fat.h>

#include "title.h"
#include "utils.h"
#include "video.h"
#include "wad.h"

/* Variables */
//static u8 wadBuffer[BLOCK_SIZE] ATTRIBUTE_ALIGN(32);

void Wad_GetID(const char * filename, char * ID)
{
	FILE *fp = fopen(filename,"rb");
	if (fp != NULL) 
		{
		fseek(fp,0xC20,SEEK_SET);
		fread(ID,1,5,fp);
		fclose(fp);
		}		

}

void Wad_GetRegion(char *ID,char * region)
{
	switch(ID[3])
	{
	case 'P':
		sprintf(region,"%s","PAL");
		break;
	case 'E':
		sprintf(region,"%s","USA");
		break;
	case 'J':
		sprintf(region,"%s","JAP");
		break;
	default:
		sprintf(region,"%s"," ");
		break;
	}
}

s32 Wad_ReadFile(FILE *fp, void *outbuf, u32 offset, u32 len)
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

s32 Wad_ReadAlloc(FILE *fp, void **outbuf, u32 offset, u32 len)
{
	void *buffer = NULL;
	s32   ret;

	/* Allocate memory */
	buffer = memalign(32, len);
	if (!buffer)
		return -1;

	/* Read file */
	ret = Wad_ReadFile(fp, buffer, offset, len);
	if (ret < 0) {
		free(buffer);
		return ret;
	}

	/* Set pointer */
	*outbuf = buffer;

	return 0;
}

s32 Wad_GetTitleID(FILE *fp, wadHeader *header, u64 *tid)
{
	signed_blob *p_tik    = NULL;
	tik         *tik_data = NULL;

	u32 offset = 0;
	s32 ret;

	// Ticket offset 
	offset += round_up(header->header_len, 64);
	offset += round_up(header->certs_len,  64);
	offset += round_up(header->crl_len,    64);

	// Read ticket 
	ret = Wad_ReadAlloc(fp, (void *)&p_tik, offset, header->tik_len);
	if (ret < 0)
		goto out;

	// Ticket data 
	tik_data = (tik *)SIGNATURE_PAYLOAD(p_tik);

	// Copy title ID 
	*tid = tik_data->titleid;

out:
	// Free memory 
	if (p_tik)
		free(p_tik);

	return ret;
}
