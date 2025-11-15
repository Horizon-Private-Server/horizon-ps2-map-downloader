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

const char* REPOS_URL = "box.rac-horizon.com/cmap_repos.txt";
const char* REPOS_LOCAL_PATH = "horizon_repos.txt";

const char* HOSTNAME = "box.rac-horizon.com";
const char* REPO_PATH = "/downloads/maps";
const char * URL_DL_NTSC_PATH = "/downloads/maps/dl/%s.%s";
const char * URL_UYA_NTSC_PATH = "/downloads/maps/uya/%s.%s";
const char * URL_UYA_PAL_PATH = "/downloads/maps/uya/%s.pal.%s";
int TV_mode = 2;

const char* GAME_FULL_NAMES[] = {
  "Deadlocked (NTSC)",
  "Up Your Arsenal (NTSC)",
  "Ratchet and Clank 3 (PAL)"
};

const char* MENU_OPTIONS[] = {
  "        Browse Maps        ",
  "       Browse Repos        ",
  "      Return to Game       "
};

const char* RETRY_EXIT_OPTIONS[] = {
  "           Retry           ",
  "           Exit            "
};

const char* GAME_OPTIONS[] = {

  "         All Games         ",
  "     Deadlocked (NTSC)     ",
  "   Up Your Arsenal (NTSC)  ",
  " Ratchet and Clank 3 (PAL) ",
  "          Return           "
};

void scr_header(void)
{
  scr_clear();
  scr_printf("Welcome to the Horizon Custom Map Downloader\n");
  scr_printf("A tool to download Custom Maps for Ratchet and Clank UYA & Deadlocked\n");
  scr_printf("You may also download the maps from https://rac-horizon.com\n");
  scr_printf("\n");
}

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
        scr_printf("\n");
        return idx;
      }
    }
  }
}

void scr_prompt_okay(void)
{
  const char* options[] = {"           Okay            "};
  scr_prompt(options, 1, 0);
}

void repo_build_db_index(struct Repo* repo, struct DbIndex* db, int game)
{
  db->ItemCount = 0;
  db->DeltaCount = 0;
  db->Name = GAME_FULL_NAMES[game];
  db->Hostname = repo->HostName;
  db->Path = repo->Path;
  db->GameCode = DB_GAME_FOLDERS[game];
  db->RegionCode = DB_GAME_REGIONS[game];
  db->Ext = DB_GAME_ZIP_EXTS[game];
}

int repo_has_game(struct Repo* repo, int game)
{
  return (repo->GameMask & (1 << game)) != 0;
}

int update_all(struct Repo* repos, int repos_count, int game, int redownload_all)
{
  int i,g;
  int updated = 0;
  int total = 0;
  char path[256];
  double average_elapsed = 0.0;

  // count downloads
  for (i = 0; i < repos_count; ++i) {
    for (g = 0; g < DB_GAME_COUNT; ++g) {
      if (!repo_has_game(&repos[i], g)) continue;
      if (game >= 0 && game != g) continue;
      total += redownload_all ? repos[i].DbSummaries[g].ItemCount : repos[i].DbSummaries[g].DeltaCount;
    }
  }

  // allocate zip buffer
  char* buffer = malloc(MAX_MAP_ZIP_SIZE);
  if (!buffer) {
    scr_printf("cannot allocate buffer\n");
    return -1;
  }

  scr_clear();
  for (i = 0; i < repos_count; ++i) {
    struct DbIndex db;
    struct Repo* repo = &repos[i];
    for (g = 0; g < DB_GAME_COUNT; ++g) {
      if (!repo_has_game(repo, g)) continue;
      if (game >= 0 && game != g) continue;

      int repo_download_count = redownload_all ? repo->DbSummaries[g].ItemCount : repo->DbSummaries[g].DeltaCount;
      if (!repo_download_count) continue;

      repo_build_db_index(repo, &db, g);
      if (db_parse(&db, 0) < 0) continue;

      int j;
      for (j = 0; j < db.ItemCount; ++j) {
        struct DbIndexItem* item = &db.Items[j];

        if (!redownload_all && item->LocalVersion == item->RemoteVersion) {
          DPRINTF("skipping %s\n", item->Name);
          continue;
        }
        
        scr_setXY(0, 0);
        scr_printf("[%d/%d]\n", updated+1, total);
        scr_printf("downloading %s... ", item->Name);

        clock_t start = clock();
        int result = db_download_item(&db, item, buffer, MAX_MAP_ZIP_SIZE);
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
    }
  }
      
  scr_printf("done\n");
  free(buffer);
  
  scr_prompt_okay();
  return 0;
}

int run_parse_repo(struct Repo* repos, int repos_count, int game)
{
  u32 pad = 0;
  int g = 0;
  const char* options[] = {
    "      Download Updates     ",
    "   (Re)Download All Maps   ",
    "          Return           "
  };

  // no repos
  if (!repos_count) return 0;
  
  // check if we have any updates
  int total_updates = 0;
  int total_maps = 0;
  int i;
  for (i = 0; i < repos_count; ++i) {
    struct Repo* repo = &repos[i];

    for (g = 0; g < DB_GAME_COUNT; ++g) {
      if (game >= 0 && game != g) continue;

      struct DbIndexSummary* db = &repo->DbSummaries[g];
      total_updates += db->DeltaCount;
      total_maps += db->ItemCount;
    }
  }

  // no maps
  if (total_maps == 0) {
    scr_printf("No maps found\n");
    scr_prompt_okay();
    return 0;
  }

  // ensure output dir exists
  for (g = 0; g < DB_GAME_COUNT; ++g) {
    if (game >= 0 && game != g) continue;

    if (db_check_mass_dir(DB_GAME_FOLDERS[g]) < 0) {
      scr_printf("Please insert a FAT32 format USB stick\n");
      scr_prompt_okay();
      return -1;
    }
  }

  // prompt
  int sel_option = 0;
  scr_printf("%d maps, %d updates found\n", total_maps, total_updates);
  if (total_updates == 0) {
    sel_option = scr_prompt(options+1, 2, 0) + 1;
  } else {
    sel_option = scr_prompt(options, 3, 0);
  }

  // check for exit
  if (sel_option == 2) // exit
    return 0;

  // update
  update_all(repos, repos_count, game, sel_option);
  return 0;
}

int handle_maps(struct Repo* repos, int repos_count)
{
  // no repos
  if (!repos_count) {
    scr_printf("need at least 1 repo subscription\n");
    scr_prompt_okay();
    return 0;
  }

  // parse all repos
  int i;
  for (i = 0; i < repos_count; ++i) {
    struct Repo* repo = &repos[i];
    int total_updates = 0;
    scr_printf("fetching %s... ", repo->Name);

    int g;
    for (g = DB_GAME_DL; g < DB_GAME_COUNT; ++g) {
      struct DbIndex db;
      repo_build_db_index(repo, &db, g);

      // get game db
      if (repo_has_game(repo, g)) {
        DPRINTF("%s has game %d\n", repo->Name, g);
        if (db_parse(&db, 0) >= 0 && db.DeltaCount > 0) {
          //scr_printf("%s %s => %d Updates\n", DB_GAME_FOLDERS[g], DB_GAME_REGIONS[g], db.DeltaCount);
        }
      }

      repo->DbSummaries[g].DeltaCount = db.DeltaCount;
      repo->DbSummaries[g].ItemCount = db.ItemCount;
      total_updates += db.DeltaCount;
    }

    scr_printf("%d update(s)\n", total_updates);
  }

  // print
  scr_printf("\n============== Browse Maps ==============\n");
  int sel_option = scr_prompt(GAME_OPTIONS, 5, 0);
  if (sel_option == 4) {
    scr_printf("bye!\n");
    return -1;
  }

  run_parse_repo(repos, repos_count, sel_option - 1);
}

struct Repo* repo_find_by_host_path(struct Repo* repos, int repos_count, char* host, char* path)
{
  int i;
  for (i = 0; i < repos_count; ++i) {
    if (strncmp(repos[i].HostName, host, sizeof(repos[i].HostName)) != 0) continue;
    if (strncmp(repos[i].Path, path, sizeof(repos[i].Path)) != 0) continue;
    return &repos[i];
  }

  return NULL;
}

void repo_print_subscriptions(struct Repo* subbed_repos, int subbed_repos_count)
{
  int total = 0;
  int index = 0;


  // count
  int i;
  for (i = 0; i < subbed_repos_count; ++i) {
    struct Repo* repo = &subbed_repos[i];
    if (repo->GameMask == 0) continue;

    ++total;
  }

  // draw
  for (i = 0; i < subbed_repos_count; ++i) {
    struct Repo* repo = &subbed_repos[i];
    if (repo->GameMask == 0) continue;

    if ((index % 10) == 0) {
      if (index > 0) {
        scr_printf("\n");
        scr_prompt_okay();
      }
      scr_header();
      scr_printf("Subscriptions\n");
    }

    // print repo name
    scr_printf("[%d/%d]\t%s ", index+1, total, repo->Name);
    
    // print which games are subscribed to
    int g;
    for (g = 0; g < DB_GAME_COUNT; ++g) {
      if (!repo_has_game(repo, g)) continue;

      scr_printf("[%s] ", DB_GAME_FOLDERS[g]);
    }

    scr_printf("\n");
    ++index;
  }

  if (total == 0) {
    scr_printf("No subscriptions\n");
  }
  scr_printf("\n");
}

int handle_repos(struct Repo* remote_repos, int remote_repo_count, struct Repo* subbed_repos, int subbed_repos_count)
{
  char repo_options[MAX_REPOS + 1][256] = {0};
  char* repo_prompt_options[MAX_REPOS + 1] = {0};
  int i;
  for (i = 0; i < (MAX_REPOS+1); ++i)
    repo_prompt_options[i] = repo_options[i];

  while (1) {
      
    snprintf(repo_options[0], sizeof(repo_options[0]), "List Subscriptions");
    for (i = 0; i < remote_repo_count; ++i) {
      snprintf(repo_options[i+1], sizeof(repo_options[i+1]), "%s", remote_repos[i].Name);
    }
    snprintf(repo_options[i+1], sizeof(repo_options[i+1]), "Return");

    // print
    scr_printf("\n============== Browse Repos ==============\n");
    int sel_option = scr_prompt((const char**)repo_prompt_options, i+2, 0);
    if (sel_option == (i+1)) {
      scr_printf("bye!\n");
      return -1;
    }

    // list
    if (sel_option == 0) {
      repo_print_subscriptions(subbed_repos, subbed_repos_count);      
      scr_prompt_okay();
      scr_header();
      continue;
    }

    struct Repo* repo = &remote_repos[sel_option-1];
    struct Repo* subbed_repo = repo_find_by_host_path(subbed_repos, subbed_repos_count, repo->HostName, repo->Path);

    // list maps
    int g;
    for (g = 0; g < DB_GAME_COUNT; ++g) {
      if (!repo_has_game(repo, g)) continue;

      int is_subbed = subbed_repo && repo_has_game(subbed_repo, g);

      struct DbIndex db;
      repo_build_db_index(repo, &db, g);
      if (db_parse_get_count(&db) < 0) continue;

      scr_printf("(%s) %s: %d total maps\n", is_subbed ? "SUBBED" : "NO SUB", GAME_FULL_NAMES[g], db.ItemCount);
    }
    scr_printf("\n");

    // prompt for subs
    int prompt_count = 0;
    int prompt_games[DB_GAME_COUNT] = {0};
    for (g = 0; g < DB_GAME_COUNT; ++g) {
      if (!repo_has_game(repo, g)) continue;

      prompt_games[prompt_count] = g;
      int is_subbed = subbed_repo && repo_has_game(subbed_repo, g);
      snprintf(repo_options[g], sizeof(repo_options[g]), "%s %s", is_subbed ? "UNSUB to" : "SUB to", GAME_FULL_NAMES[g]);
      ++prompt_count;
    }
    snprintf(repo_options[prompt_count], sizeof(repo_options[prompt_count]), "Return");
    
    sel_option = scr_prompt((const char**)repo_prompt_options, prompt_count+1, 0);
    if (sel_option < prompt_count) {
      int game = prompt_games[sel_option];
      int gamemask = 1 << game;
      int is_subbed = subbed_repo && repo_has_game(subbed_repo, game);
      if (is_subbed) {
        subbed_repo->GameMask &= ~gamemask;
        db_repo_save(REPOS_LOCAL_PATH, subbed_repos, subbed_repos_count);
        scr_printf("Unsubscribed to %s\n", subbed_repo->Name);
        scr_prompt_okay();
      } else if (subbed_repo) {
        subbed_repo->GameMask |= gamemask;
        db_repo_save(REPOS_LOCAL_PATH, subbed_repos, subbed_repos_count);
        scr_printf("Subscribed to %s\n", subbed_repo->Name);
        scr_prompt_okay();
      } else if (subbed_repos_count < MAX_REPOS) {
        subbed_repo = &subbed_repos[subbed_repos_count];
        memcpy(subbed_repo, repo, sizeof(struct Repo));
        subbed_repos_count += 1;
        subbed_repo->GameMask = gamemask;
        db_repo_save(REPOS_LOCAL_PATH, subbed_repos, subbed_repos_count);
        scr_printf("Subscribed to %s\n", subbed_repo->Name);
        scr_prompt_okay();
      }
    }
    scr_header();
  }
}

int main(int argc, char *argv[])
{
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

  DPRINTF("DBINDEX STRUCT SIZE %x\n", sizeof(struct DbIndex));
  DPRINTF("REPO STRUCT SIZE %x\n", sizeof(struct Repo));
  DPRINTF("REPOS MALLOC SIZE %x\n", sizeof(struct Repo) * MAX_REPOS);
  struct Repo* remote_repos = (struct Repo*)malloc(sizeof(struct Repo) * MAX_REPOS);
  struct Repo* subbed_repos = (struct Repo*)malloc(sizeof(struct Repo) * MAX_REPOS);

retry:;

  scr_header();
  db_check_mass(REPOS_LOCAL_PATH);

  // fetch repos
  scr_printf("fetching repositories...\n");
  int remote_repo_count = db_fetch_remote_repos(REPOS_URL, remote_repos, MAX_REPOS);
  int subbed_count = db_fetch_local_repos(REPOS_LOCAL_PATH, subbed_repos, MAX_REPOS);

  while (1) {
    scr_header();
    scr_printf("%d repo subscriptions found.\n", subbed_count);
    scr_printf("\n============== Main Menu ==============\n");
    switch (scr_prompt(MENU_OPTIONS, 3, subbed_count==0?1:0))
    {
      case 0:
      {
        handle_maps(subbed_repos, subbed_count);
        break;
      }
      case 1:
      {
        handle_repos(remote_repos, remote_repo_count, subbed_repos, subbed_count);
        subbed_count = db_fetch_local_repos(REPOS_LOCAL_PATH, subbed_repos, MAX_REPOS);
        break;
      }
      case 2: goto end;
    }
  }

end:
  free(remote_repos);
  free(subbed_repos);

	ps2ipDeinit();
	NetManDeinit();
	SifExitRpc();
  exit(0);
  return 0;
}
