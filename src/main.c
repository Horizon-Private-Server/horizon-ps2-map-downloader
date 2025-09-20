/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <tamtypes.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <debug.h>
#include <libpad.h>
#include <unistd.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <string.h>
#include <netman.h>
#include <ps2ip.h>
#include <tcpip.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/fcntl.h>


#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <fileXio.h>

#include "db.h"

extern unsigned char ps2dev9_irx[];
extern unsigned int size_ps2dev9_irx;

extern unsigned char smap_irx[];
extern unsigned int size_smap_irx;

extern unsigned char netman_irx[];
extern unsigned int size_netman_irx;

extern unsigned char usbd_irx[];
extern unsigned int size_usbd_irx;

extern unsigned char usbhdfsd_irx[];
extern unsigned int size_usbhdfsd_irx;

int client_init(void);
int pad_init(void);
int pad_read(u32 * pad);

const char* HOSTNAME = "box.rac-horizon.com";
const char * URL_DL_NTSC_PATH = "/downloads/maps/dl/%s.%s";
const char * URL_UYA_NTSC_PATH = "/downloads/maps/uya/%s.%s";
const char * URL_UYA_PAL_PATH = "/downloads/maps/uya/%s.pal.%s";
int TV_mode = 2;

int scr_prompt(const char* options[], int options_count, int default_option)
{
  u32 pad = 0;
  int idx = default_option;
  int y = scr_getY();

  while (1)
  {
    // print
    scr_clearline(y);
    scr_setXY(0, y);
    scr_printf("Choose: < %s >\n", options[idx]);

    // read pad
    while (pad_read(&pad))
    {
      if (pad & PAD_LEFT) {
        idx--;
        if (idx < 0) idx = options_count - 1;
        break;
      } else if (pad & PAD_RIGHT) {
        ++idx;
        if (idx >= options_count) idx = 0;
        break;
      } else if (pad & PAD_CROSS) {
        return idx;
      }
    }
  }
}

int update_all(struct DbIndex* db, int redownload_all)
{
  int i;
  int updated = 0;
  int total = redownload_all ? db->ItemCount : db->DeltaCount;
  char path[256];
  double average_elapsed = 0.0;

  char* buffer = malloc(MAX_MAP_ZIP_SIZE);
  if (!buffer) {
    scr_printf("cannot allocate buffer\n");
    return -1;
  }

  scr_clear();
  for (i = 0; i < db->ItemCount; ++i) {
    struct DbIndexItem* item = &db->Items[i];

    if (!redownload_all && item->LocalVersion == item->RemoteVersion) {
      DPRINTF("skipping %s\n", item->Name);
      continue;
    }
    
    scr_setXY(0, 0);
    scr_printf("[%d/%d]\n", updated+1, total);
    scr_printf("downloading %s... ", item->Name);

    clock_t start = clock();
    int result = db_download_item(db, item, buffer, MAX_MAP_ZIP_SIZE);
    if (result < 0) {
      scr_printf("error downloading %s (%d)\n", item->Name, result);
      free(buffer);
      return -1;
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    average_elapsed += elapsed;
    ++updated;
    
    scr_printf("\n\n");
    scr_printf("downloaded %s in %.2f minutes\n", item->Name, elapsed / 60.0);
    scr_printf("eta %.2f minutes\n", ((average_elapsed / updated) * (total - updated)) / 60.0);
    scr_printf("\n\n");
    //break;
  }
      
  // write global version
  db_write_global_version(db);
  scr_printf("done\n");
  free(buffer);
  
  const char* options[] = {"           Okay            "};
  scr_prompt(options, 1, 0);
  return 0;
}

int run_parse_db(struct DbIndex* db)
{
  u32 pad = 0;
  const char* options[] = {
    "      Download Updates     ",
    "   (Re)Download All Maps   ",
    "          Return           "
  };
  
  // ensure output dir exists
  if (db_check_mass_dir(db) < 0) {
    scr_printf("Please insert a FAT32 format USB stick (CROSS)\n");
    while (pad_read(&pad)) {
      if (pad & PAD_CROSS) break;
    }
    return -1;
  }

  // get db
  if (db_parse(db) < 0) {
    scr_printf("Unable to connect to the horizon maps server (CROSS)\n");
    while (pad_read(&pad)) {
      if (pad & PAD_CROSS) break;
    }
    return -1;
  }

  // no updates
  int sel_option = 0;
  if (db->DeltaCount == 0) {
    scr_printf("No updates available\n");
    sel_option = scr_prompt(options+1, 2, 0) + 1;
  } else {
    sel_option = scr_prompt(options, 3, 0);
  }

  // ask to download
  scr_printf("\n");
  switch (sel_option)
  {
    case 0: // download updates
    {
      update_all(db, 0);
      break;
    }
    case 1: // download all maps
    {
      update_all(db, 1);
      break;
    }
    case 2: // exit
    {
      break;
    }
  }

  return 0;
}

int main(int argc, char *argv[])
{
  u32 pad = 0;
  struct DbIndex db;

	//Reboot IOP
  SifInitRpc(0);
	while(!SifIopReset("", 0)){};
	while(!SifIopSync()){};

	//Initialize SIF services
	SifInitRpc(0);
	SifLoadFileInit();
	SifInitIopHeap();
  sbv_patch_enable_lmb();
  sbv_patch_disable_prefix_check();
  sbv_patch_fileio();
  
	//Load modules
  SifLoadModule("rom0:SIO2MAN", 0, NULL);
  SifLoadModule("rom0:IOMAN", 0, NULL);
  SifLoadModule("rom0:PADMAN", 0, NULL);
	SifExecModuleBuffer(ps2dev9_irx, size_ps2dev9_irx, 0, NULL, NULL);
	SifExecModuleBuffer(netman_irx, size_netman_irx, 0, NULL, NULL);
	SifExecModuleBuffer(smap_irx, size_smap_irx, 0, NULL, NULL);
	SifExecModuleBuffer(usbd_irx, size_usbd_irx, 0, NULL, NULL);
	SifExecModuleBuffer(usbhdfsd_irx, size_usbhdfsd_irx, 0, NULL, NULL);

	init_scr();
  scr_setCursor(0);
  pad_init();
  sleep(1);
  if (client_init())
    goto end;

  int sel_option = 0;
  const char* options[] = {
    "     Deadlocked (NTSC)     ",
    "   Up Your Arsenal (NTSC)  ",
    " Ratchet and Clank 3 (PAL) ",
    "           Exit            "
  };

  while (1)
  {
    // print
    scr_clear();
    scr_printf("Welcome to the Horizon Custom Map Downloader\n");
    scr_printf("A tool to download Custom Maps for Ratchet and Clank UYA & Deadlocked\n");
    scr_printf("You may also download the maps from https://rac-horizon.com\n");
    scr_printf("\n");
    sel_option = scr_prompt(options, 4, sel_option);

    scr_printf("\n");
    switch (sel_option)
    {
      case 0: // deadlocked
      {
        strncpy(db.Name, options[sel_option], sizeof(db.Name));
        strncpy(db.Hostname, HOSTNAME, sizeof(db.Hostname));
        strncpy(db.GameCode, "dl", sizeof(db.GameCode));
        strncpy(db.RegionCode, "ntsc", sizeof(db.RegionCode));
        strncpy(db.Ext, "zip", sizeof(db.Ext));
        run_parse_db(&db);
        break;
      }
      case 1: // uya ntsc
      {
        strncpy(db.Name, options[sel_option], sizeof(db.Name));
        strncpy(db.Hostname, HOSTNAME, sizeof(db.Hostname));
        strncpy(db.GameCode, "uya", sizeof(db.GameCode));
        strncpy(db.RegionCode, "ntsc", sizeof(db.RegionCode));
        strncpy(db.Ext, "ntsc.zip", sizeof(db.Ext));
        run_parse_db(&db);
        break;
      }
      case 2: // uya pal
      {
        strncpy(db.Name, options[sel_option], sizeof(db.Name));
        strncpy(db.Hostname, HOSTNAME, sizeof(db.Hostname));
        strncpy(db.GameCode, "uya", sizeof(db.GameCode));
        strncpy(db.RegionCode, "pal", sizeof(db.RegionCode));
        strncpy(db.Ext, "pal.zip", sizeof(db.Ext));
        run_parse_db(&db);
        break;
      }
      case 3: // exit
      {
        scr_printf("bye!\n");
        goto end;
      }
    }
  }

end:
	ps2ipDeinit();
	NetManDeinit();
	SifExitRpc();
  exit(0);
  return 0;
}
