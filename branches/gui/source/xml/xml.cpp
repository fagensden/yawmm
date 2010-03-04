#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

/*Include these for mxml*/
#include <fat.h>
#include <mxml.h>
#include "demo.h"
#include "menu.h"

void CreateXmlFile(const char* filename, struct SSettingsSMB *Settings)
{
	mxml_node_t *xml;
	mxml_node_t *data;

	xml = mxmlNewXML("1.0");

	data = mxmlNewElement(xml, "smbconfig");

   //Create config value
	mxmlElementSetAttr(data, "username",Settings->SMB_USER);
	mxmlElementSetAttr(data, "password",Settings->SMB_PWD);
    mxmlElementSetAttr(data, "smbsharename",Settings->SMB_SHARE);
    mxmlElementSetAttr(data, "smbip",Settings->SMB_IP);
	
   /*save the xml file to a file*/
   FILE *fp;
   fp = fopen(filename, "w");

   mxmlSaveFile(xml, fp, MXML_NO_CALLBACK);
   
   /*clean up*/
   fclose(fp);
   mxmlDelete(data);
   mxmlDelete(xml);
}

int LoadXmlFile(const char* filename, struct SSettingsSMB *Settings)
{
	FILE *fp;
	mxml_node_t *tree;
	mxml_node_t *data;

	/*Load our xml file! */
	fp = fopen(filename, "r");
	
	if (fp==NULL) 
	{
	//WindowPrompt(filename, "Fehler", "Back",0);
	fclose(fp);
	return -1;
	}
	tree = mxmlLoadFile(NULL, fp, MXML_NO_CALLBACK);
	fclose(fp);

	/*Load and printf our values! */
	/* As a note, its a good idea to normally check if node* is NULL */
	data = mxmlFindElement(tree, tree, "smbconfig", NULL, NULL, MXML_DESCEND);
	
	snprintf(Settings->SMB_USER,64,"%s",mxmlElementGetAttr(data,"username"));
	//strcpy(Settings->SMB_PWD,mxmlElementGetAttr(data,"password"));
	snprintf(Settings->SMB_PWD,64,"%s",mxmlElementGetAttr(data,"password"));
	//strcpy(Settings->SMB_SHARE,mxmlElementGetAttr(data,"smbsharename"));
	snprintf(Settings->SMB_SHARE,64,"%s",mxmlElementGetAttr(data,"smbsharename"));
	//strcpy(Settings->SMB_IP,(char*)mxmlElementGetAttr(data,"smbip")); 
	snprintf(Settings->SMB_IP,20,"%s",mxmlElementGetAttr(data,"smbip")); 
	
	mxmlDelete(data);
	mxmlDelete(tree);
	return 1;
}
