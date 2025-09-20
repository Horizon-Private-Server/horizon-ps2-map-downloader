
#define MAX_DB_INDEX_ITEMS                              (128)
#define DB_INDEX_ITEM_NAME_MAX_LEN                      (512)
#define DB_INDEX_ITEM_FILENAME_MAX_LEN                  (512)
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
  char Name[256];
  char Hostname[256];
  char GameCode[16];
  char RegionCode[16];
  char Ext[16];
};

typedef void (*downloadCallback_func)(int bytes_downloaded, int total_bytes);
typedef void (*writeCallback_func)(const char* filename, int bytes_written, int total_bytes);

int db_check_mass_dir(struct DbIndex* db);
int db_parse(struct DbIndex* db);
int db_download_item(struct DbIndex* db, struct DbIndexItem* item, char* buffer, int buffer_size);
int db_write_global_version(struct DbIndex* db);
int db_read_map_version(struct DbIndex* db, struct DbIndexItem* item);
