/****************************************************************************
 * File operations
 *
 * Written by nIxx
 ****************************************************************************/

#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <unistd.h>

bool checkExt(const char * filename,const char * extension)
{
int size = strlen(filename);
char fext[4];
strcpy(fext,filename+(size-3));
if(!strcmp(fext,extension)) return true;

return false;
}

bool checkfile(const char* filename)
{
FILE *fp = fopen(filename,"rb");
if(fp!=NULL) {fclose(fp);return true;}

return false;
}

int readFileMemory(const char * filename,void **buffer, long *filesize)
{
long fsize;
void * fbuffer;

//Open File
FILE *fp = fopen(filename,"rb");
if(fp==NULL) return 0;
//Get Size
fseek(fp,0,SEEK_END);
fsize = ftell(fp);
rewind(fp);

//Read file in buffer
fbuffer = malloc(fsize);
if(fbuffer == NULL) return -1;
fread(fbuffer,1,fsize,fp);

*buffer = fbuffer;
*filesize = fsize;
fclose(fp);
return 1;
}

void copyFile(const char * filesrc,const char * filedest )
{
FILE * fp = fopen(filesrc,"rb");
fseek(fp,0,SEEK_END);
long size = ftell(fp);
rewind(fp);
u8 * buffer = (u8*)malloc(sizeof(u8)*size);

if (buffer==NULL) 
{
//WindowPrompt("Error","buffer fault", NULL,"Back");
fclose(fp);
return;
} 

int result = fread(buffer,1,size,fp);
fclose(fp);

if (result>0) {
	FILE *fpdest = fopen(filedest,"wb");
	fwrite(buffer,1,result,fpdest);
	fclose(fpdest);
	free(buffer);
	//WindowPrompt("Success","File copied", "Okay",NULL);
	} else 
	{	
	free(buffer);
	//WindowPrompt("Error","Filesize error", "Okay",NULL);
	return;
	}

}

u64 getFileSize(const char * filepath)
{
  struct stat filestat;

  if (stat(filepath, &filestat) != 0)
    return 0;

  return filestat.st_size;
}
