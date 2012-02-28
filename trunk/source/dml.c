#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <gccore.h>
#include <ogc/es.h>
#include <ogc/video_types.h>
#include <dirent.h>
#include "globals.h"
#include "fsop/fsop.h"
#include "devices.h"

#define SEP 0xFF
#define SEP2 0x1

#define BC 0x0000000100000100ULL
#define MIOS 0x0000000100000101ULL

/** Base address for video registers. */
#define MEM_VIDEO_BASE (0xCC002000)
#define IOCTL_DI_DVDLowAudioBufferConfig 0xE4

#define VIDEO_MODE_NTSC 0
#define VIDEO_MODE_PAL 1
#define VIDEO_MODE_PAL60 2
#define VIDEO_MODE_NTSC480P 3
#define VIDEO_MODE_PAL480P 4

syssram* __SYS_LockSram();
u32 __SYS_UnlockSram(u32 write);
u32 __SYS_SyncSram(void);

s32 setstreaming()
	{
	char __di_fs[] ATTRIBUTE_ALIGN(32) = "/dev/di";
	u32 bufferin[0x20] __attribute__((aligned(32)));
	u32 bufferout[0x20] __attribute__((aligned(32)));
	s32 __dvd_fd = -1;
	
	u8 ioctl;
	ioctl = IOCTL_DI_DVDLowAudioBufferConfig;

	__dvd_fd = IOS_Open(__di_fs,0);
	if(__dvd_fd < 0) return __dvd_fd;

	memset(bufferin, 0, 0x20);
	memset(bufferout, 0, 0x20);

	bufferin[0] = (ioctl << 24);

	if ( (*(u32*)0x80000008)>>24 )
		{
		bufferin[1] = 1;
		if( ((*(u32*)0x80000008)>>16) & 0xFF )
			bufferin[2] = 10;
		else 
			bufferin[2] = 0;
		}
	else
		{		
		bufferin[1] = 0;
		bufferin[2] = 0;
		}			
	DCFlushRange(bufferin, 0x20);
	
	int Ret = IOS_Ioctl(__dvd_fd, ioctl, bufferin, 0x20, bufferout, 0x20);
	
	IOS_Close(__dvd_fd);
	
	return ((Ret == 1) ? 0 : -Ret);
	}

static void SetGCVideoMode (void)
	{
	syssram *sram;
	sram = __SYS_LockSram();

	if (config.dmlvideomode == DMLVIDEOMODE_NTSC)
		{
		sram->flags = sram->flags & ~(1 << 0);	// Clear bit 0 to set the video mode to NTSC
		} 
	else
		{
		sram->flags = sram->flags |  (1 << 0);	// Set bit 0 to set the video mode to PAL
		}
	
	__SYS_UnlockSram(1); // 1 -> write changes
	
	while(!__SYS_SyncSram());
	
	// TVPal528IntDf
	
	u32 *xfb;
	static GXRModeObj *rmode;
	
	//config.dmlvideomode = DMLVIDEOMODE_PAL;
	
	if (config.dmlvideomode == DMLVIDEOMODE_PAL)
		{
		rmode = &TVPal528IntDf;
		*(u32*)0x800000CC = VI_PAL;
		}
	else
		{
		rmode = &TVNtsc480IntDf;
		*(u32*)0x800000CC = VI_NTSC;
		}

	VIDEO_SetBlack(TRUE);
	VIDEO_Configure(rmode);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb);

	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

	VIDEO_SetBlack(FALSE);
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
	}

s32 StartMIOS (void)
	{
	s32 ret;
	
	//ret = setstreaming ();
	
	SetGCVideoMode ();
	
	tikview view ATTRIBUTE_ALIGN(32);
	
	ret = ES_GetTicketViews(BC, &view, 1);
	if (ret != 0) return -1;

	// Tell DML to boot the game from sd card
	*(u32 *)0x80001800 = 0xB002D105;
	DCFlushRange((void *)(0x80001800), 4);
	ICInvalidateRange((void *)(0x80001800), 4);			
	
	*(volatile unsigned int *)0xCC003024 |= 7;
	
	ret = ES_LaunchTitle(BC, &view);
	
	return -102;
	}
	
	
#define MAXGAMES 30
#define MAXROW 10

static bool GetName (int dev, char *id, char *name)
	{
	char path[128];
	
	if (dev == DEV_SD)
		sprintf (path, "%s://games/%s/game.iso", devices_Get(dev), id);
	else
		sprintf (path, "%s://ngc/%s/game.iso", devices_Get(dev), id);
		
	FILE *f;
	f = fopen(path, "rb");
	if (!f)	
		{
		*name = '\0';
		return false;
		}
	
	fseek( f, 0x20, SEEK_SET);
	fread(name, 1, 32, f);
	fclose(f);
	
	name[31] = 0;
	return true;
	}

int DMLRun (char *id, u32 videomode)
	{
	char path[128];

	if (!devices_Get(DEV_SD)) return 0;
	
	if (videomode == 0) // GAME
		{
		if (id[3] == 'E' || id[3] == 'J' || id[3] == 'N')
			config.dmlvideomode = DMLVIDEOMODE_NTSC;
		else
			config.dmlvideomode = DMLVIDEOMODE_PAL;
		}
	if (videomode == 1) // WII
		{
		if (CONF_GetRegion() == CONF_REGION_EU)
			config.dmlvideomode = DMLVIDEOMODE_PAL;
		else
			config.dmlvideomode = DMLVIDEOMODE_NTSC;
		}
	if (videomode == 2)
		config.dmlvideomode = DMLVIDEOMODE_NTSC;

	if (videomode == 3)
		config.dmlvideomode = DMLVIDEOMODE_PAL;

	sprintf (path, "%s://games/boot.bin", devices_Get(DEV_SD));
	
	FILE *f;
	f = fopen(path, "wb");
	if (!f)	return -1;
	fwrite(id, 1, 6, f);
	fclose(f);
	
 	memcpy ((char *)0x80000000, id, 6);
	
	Shutdown (0);
	
	StartMIOS ();
	return 1;
	}
	

void DMLResetCache (void)
	{
	char cachepath[128];
	sprintf (cachepath, "%s://ploader/dml.cfg", vars.defMount);
	unlink (cachepath);
	}

#define BUFFSIZE (1024*64)

static void cb_DML (void)
	{
	Video_WaitPanel (TEX_HGL, "Please wait...|Searching gamecube games");
	}

char *DMLScanner  (bool reset)
	{
	static bool xcheck = true; // do that one time only
	DIR *pdir;
	struct dirent *pent;
	char cachepath[128];
	char path[128];
	char name[32];
	char src[32];
	char b[128];
	FILE *f;
	char *buff = calloc (1, BUFFSIZE); // Yes, we are wasting space...
	
	sprintf (cachepath, "%s://ploader/dml.cfg", vars.defMount);

	if (reset == 0)
		{
		f = fopen (cachepath, "rb");
		if (!f) 
			reset = 1;
		else
			{
			fread (buff, 1, BUFFSIZE-1, f);
			fclose (f);
			
			buff[BUFFSIZE-1] = 0;
			}
		}
	
	if (reset == 1)
		{
		if (!devices_Get(DEV_SD)) return 0;
		
		sprintf (path, "%s://games", devices_Get(DEV_SD));
		
		fsop_GetFolderKb (path, 0);
		
		Debug ("DML: scanning %s", path);
		
		pdir=opendir(path);
		
		while ((pent=readdir(pdir)) != NULL) 
			{
			//if (strcmp (pent->d_name, ".") && strcmp (pent->d_name, ".."))
			if (strlen (pent->d_name) == 6 || strlen (pent->d_name) == 7)
				{
				Video_WaitPanel (TEX_HGL, "Please wait...|Searching gamecube games");
				
				bool skip = false;
				
				if (xcheck && devices_Get(DEV_USB))
					{
					char sdp[256], usbp[256];
					
					sprintf (sdp, "%s://games/%s", devices_Get(DEV_SD), pent->d_name);
					sprintf (usbp, "%s://ngc/%s", devices_Get(DEV_USB), pent->d_name);
					
					if (fsop_DirExist (usbp))
						{
						int sdkb, usbkb;
						
						sdkb = fsop_GetFolderKb (sdp, cb_DML);
						usbkb = fsop_GetFolderKb (usbp, cb_DML);
						
						if (abs (sdkb - usbkb) > 2) // Let 2kb difference for codes
							{
							char mex[256];
							fsop_KillFolderTree (sdp, cb_DML);
							
							sprintf (mex, "Folder '%s' removed\n as it has the wrong size", sdp);
							grlib_menu (mex, "   OK   ");
							skip = true;
							}
						}
					}
		
				if (!skip)
					{
					if (!GetName (DEV_SD, pent->d_name, name)) continue;
					sprintf (b, "%s%c%s%c%d%c", name, SEP, pent->d_name, SEP, DEV_SD, SEP);
					strcat (buff, b);
					}
				}
			}
			
		closedir(pdir);
		
		xcheck = false;

		if (devices_Get(DEV_USB))
			{
			sprintf (path, "%s://ngc", devices_Get(DEV_USB));
			
			Debug ("DML: scanning %s", path);
			
			pdir=opendir(path);
			
			while ((pent=readdir(pdir)) != NULL) 
				{
				//if (strcmp (pent->d_name, ".") && strcmp (pent->d_name, ".."))
				sprintf (src, "%c%s%c", SEP, pent->d_name, SEP); // make sure to find the exact name
				if ((strlen (pent->d_name) == 6 || strlen (pent->d_name) == 7) && strstr (buff, src) == NULL)	// Make sure to not add the game twice
					{
					Video_WaitPanel (TEX_HGL, "Please wait...|Searching gamecube games");
					if (!GetName (DEV_USB, pent->d_name, name)) continue;
					sprintf (b, "%s%c%s%c%d%c", name, SEP, pent->d_name, SEP, DEV_USB, SEP);
					strcat (buff, b);
					}
				}
				
			closedir(pdir);
			}
		
		Debug ("WBFSSCanner: writing cache file");
		f = fopen (cachepath, "wb");
		if (f) 
			{
			fwrite (buff, 1, strlen(buff)+1, f);
			fclose (f);
			}
		}

	int i, l;
	
	l = strlen (buff);
	for (i = 0; i < l; i++)
		if (buff[i] == SEP || buff[i] == SEP2)
			buff[i] = 0;

	return buff;
	}
	
/*

DMLRemove will prompt to remove same games from sd to give space to new one

*/

int DMLInstall (char *gamename, size_t reqKb)
	{
	int ret;
	int i = 0, j;
	DIR *pdir;
	struct dirent *pent;
	char path[128];
	char menu[2048];
	char buff[64];
	char name[64];
	char title[256];
	
	char files[MAXGAMES][64];
	size_t sizes[MAXGAMES];

	size_t devKb = 0;
	
	Debug ("DMLInstall (%s): Started", gamename);

	if (!devices_Get(DEV_SD))
		{
		Debug ("DMLInstall (%s): ERR SD Device invalid", gamename);
		return 0;
		}
	
	sprintf (path, "%s://games", devices_Get(DEV_SD));

	devKb = fsop_GetFreeSpaceKb (path);
	if (devKb > reqKb) 
		{
		Debug ("DMLInstall (%s): OK there is enaught space", gamename);
		return 1; // We have the space
		}
	
	while (devKb < reqKb)
		{
		*menu = '\0';
		i = 0;
		j = 0;
		
		pdir=opendir(path);
		
		while ((pent=readdir(pdir)) != NULL) 
			{
			if (strlen (pent->d_name) ==  6)
				{
				strcpy (files[i], pent->d_name);
				
				GetName (DEV_SD, files[i], name);
				if (strlen (name) > 20)
					{
					name[12] = 0;
					strcat(name, "...");
					}
					
				sprintf (buff, "%s/%s", path, files[i]);
				sizes[i] = fsop_GetFolderKb (buff, NULL);
				grlib_menuAddItem (menu, i, "%s (%d Mb)", name, sizes[i] / 1000);
				
				i++;
				j++;
			
				if (j == MAXROW)
					{
					grlib_menuAddColumn (menu);
					j = 0;
					}
				
				if (i == MAXGAMES)
					{
					Debug ("DMLInstall (%s): Warning... to many games", gamename);

					break;
					}
				}
			}
		
		closedir(pdir);

		Debug ("DMLInstall (%s): found %d games on sd", gamename, i);
		
		sprintf (title, "You must free %u Mb to install %s\nClick on game to remove it from SD, game size is %u Mb), Press (B) to close", 
			(reqKb - devKb) / 1000, gamename, reqKb / 1000);

		ret = grlib_menu (title, menu);
		if (ret == -1)
			{
			Debug ("DMLInstall (%s): aborted by user", gamename);
			return 0;
			}
			
		if (ret >= 0)
			{
			char gamepath[128];
			sprintf (gamepath, "%s://games/%s", devices_Get(DEV_SD), files[ret]);
			
			Debug ("DMLInstall deleting '%s'", gamepath);
			fsop_KillFolderTree (gamepath, NULL);
			
			DMLResetCache (); // rebuild the cache next time
			}
			
		devKb = fsop_GetFreeSpaceKb (path);
		if (devKb > reqKb)
			{
			Debug ("DMLInstall (%s): OK there is enaught space", gamename);
			return 1; // We have the space
			}
		}
	
	Debug ("DMLInstall (%s): Something gone wrong", gamename);
	return 0;
	}
	