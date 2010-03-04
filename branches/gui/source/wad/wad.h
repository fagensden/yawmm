#ifndef _WAD_H_
#define _WAD_H_

#ifdef __cplusplus
extern "C"
{
#endif

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


/* Prototypes */
s32 Wad_ReadFile(FILE *fp, void *outbuf, u32 offset, u32 len);
s32 Wad_ReadAlloc(FILE *fp, void **outbuf, u32 offset, u32 len);
s32 Wad_GetTitleID(FILE *fp, wadHeader *header, u64 *tid);
void Wad_GetID(const char * filename, char * ID);
void Wad_GetRegion(char * ID,char * region);

#ifdef __cplusplus
}
#endif


#endif
