
#define MAX_REPOS                                       (32)
#define MAX_DB_INDEX_ITEMS                              (128)
#define DB_INDEX_ITEM_NAME_MAX_LEN                      (64)
#define DB_INDEX_ITEM_FILENAME_MAX_LEN                  (128)
#define MAX_MAP_ZIP_SIZE                                (1024 * 1024 * 20)

#ifdef DEBUG
    #define DPRINTF(fmt, ...)       \
        printf("%s"fmt, "", ##__VA_ARGS__);
#else
    #define DPRINTF(fmt, ...) 
#endif

#ifdef DEBUG
    #define SCR_DEBUG(fmt, ...)       \
        scr_printf("%s"fmt, "", ##__VA_ARGS__);
#else
    #define SCR_DEBUG(fmt, ...) 
#endif

enum DbGames
{
  DB_GAME_DL = 0,
  DB_GAME_UYA = 1,
  DB_GAME_RC3 = 2,
  DB_GAME_COUNT
};

enum DbGameMask
{
  DB_GAMEMASK_DL = 1 << DB_GAME_DL,
  DB_GAMEMASK_UYA = 1 << DB_GAME_UYA,
  DB_GAMEMASK_RC3 = 1 << DB_GAME_RC3,
  DB_GAMEMASK_ALL = DB_GAMEMASK_DL | DB_GAMEMASK_UYA | DB_GAMEMASK_RC3
};

struct DbIndexItem
{
  int RemoteVersion;
  int LocalVersion;
  char Name[DB_INDEX_ITEM_NAME_MAX_LEN];
  char Filename[DB_INDEX_ITEM_FILENAME_MAX_LEN];
};

struct DbIndex
{
  int ItemCount;
  int DeltaCount;
  struct DbIndexItem Items[MAX_DB_INDEX_ITEMS];
  char* Name;
  char* Hostname;
  char* Path;
  char* GameCode;
  char* RegionCode;
  char* Ext;
};

struct DbIndexSummary
{
  int ItemCount;
  int DeltaCount;
};

struct Repo
{
  char Name[256];
  char HostName[256];
  char Path[512];
  int GameMask;
  struct DbIndexSummary DbSummaries[DB_GAME_COUNT];
};

typedef void (*downloadCallback_func)(int bytes_downloaded, int total_bytes);
typedef void (*writeCallback_func)(const char* filename, int bytes_written, int total_bytes);

extern const char* DB_GAME_FOLDERS[DB_GAME_COUNT];
extern const char* DB_GAME_REGIONS[DB_GAME_COUNT];
extern const char* DB_GAME_ZIP_EXTS[DB_GAME_COUNT];

int db_check_mass(char* filepath);
int db_check_mass_dir(char* folder);
int db_parse_get_count(struct DbIndex* db);
int db_parse(struct DbIndex* db, int print_lines);
int db_download_item(struct DbIndex* db, struct DbIndexItem* item, char* buffer, int buffer_size);
int db_read_map_version(struct DbIndex* db, struct DbIndexItem* item);
int db_fetch_remote_repos(const char* url, struct Repo* repos, int repos_count);
int db_fetch_local_repos(const char* path, struct Repo* repos, int repos_count);
int db_repo_save(const char* path, struct Repo* repos, int repos_count);
