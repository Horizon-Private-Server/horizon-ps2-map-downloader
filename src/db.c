#include <stdio.h>
#include <stdlib.h>
#include <tamtypes.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <debug.h>
#include <unistd.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <string.h>
#include <netman.h>
#include <libpad.h>
#include <ps2ip.h>
#include <tcpip.h>
#include <loadfile.h>
#include <dirent.h>
#include <sbv_patches.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/fcntl.h>

#include "miniz.h"
#include "db.h"

#define BUF_SIZE                  (1024 * 2)
#define USB_WRITE_BLOCK_SIZE      (256)

const char * DRIVE_MASS = "mass";
const char * DRIVE_HOST = "host";
const char * LOCAL_PATH = "%s:%s";
const char * LOCAL_BINARY_PATH = "%s:%s/%s.%s";
const char * INDEX_PATH = "%sindex_%s_%s.txt";
//const char * MAP_IMAGE_PATH = "/downloads/maps/assets/images/%s.jpg";
const char * MAP_BINARY_PATH = "%s%s/%s.%s";
const char * MAP_VERSION_EXT = "version";

const char* DB_GAME_FOLDERS[DB_GAME_COUNT] = {
  [DB_GAME_DL] = "dl",
  [DB_GAME_UYA] = "uya",
  [DB_GAME_RC3] = "uya"
};

const char* DB_GAME_REGIONS[DB_GAME_COUNT] = {
  [DB_GAME_DL] = "ntsc",
  [DB_GAME_UYA] = "ntsc",
  [DB_GAME_RC3] = "pal"
};

const char* DB_GAME_ZIP_EXTS[DB_GAME_COUNT] = {
  [DB_GAME_DL] = "zip",
  [DB_GAME_UYA] = "ntsc.zip",
  [DB_GAME_RC3] = "pal.zip"
};

int client_connect(const char* hostname);
int client_get(int socket, const char* hostname, const char* path, char* output, int output_size);

int use_host = 0;

int parse_http_response_content_length(char* buf)
{
  int contentLength = 0;

  // read content length
  char* substrbuf = strstr(buf, "Content-Length: ");
  if (substrbuf)
    sscanf(substrbuf, "Content-Length: %d", &contentLength);

  return contentLength;
}

int parse_http_response_content_type(char* buf, char* output)
{
  // read content length
  char* substrbuf = strstr(buf, "Content-Type: ");
  if (!substrbuf)
    return -1;

  return sscanf(substrbuf, "Content-Type: %s\r\n", output) == 1;
}

int parse_http_response_content(char* buf, int buf_size, char* output, int output_size)
{
  // read content length
  char* substrbuf = strstr(buf, "\r\n\r\n");
  if (!substrbuf)
    return -1;

  int content_size = buf_size - ((substrbuf - buf) + 4);
  int read = (content_size < output_size) ? content_size : output_size;
  memcpy(output, substrbuf + 4, read);
  return read;
}

int parse_http_response(char* buf, int buf_size, int* response_code, int* content_length, char* output_content, int output_content_size, char* output_content_type, int output_content_type_size)
{
  *response_code = 0;
  *content_length = 0;
  if (!buf)
    return -1;

  // initialize buffers
  memset(output_content_type, 0, output_content_type_size);

  // read response code
  sscanf(buf, "HTTP/1.1 %d", response_code);
  if (*response_code != 200)
    return 0;

  // read content
  *content_length = parse_http_response_content_length(buf);
  parse_http_response_content_type(buf, output_content_type);
  return parse_http_response_content(buf, buf_size, output_content, (*content_length < output_content_size) ? *content_length : output_content_size);
}

int http_get(const char* hostname, const char* path, char* output, int output_length, downloadCallback_func callback)
{
  char buffer[2048];
  char content_type_buffer[2048];
  int response, content_length;
  int total_read = 0;

  DPRINTF("http_get %s\n", path);

  // connect
  int socket = client_connect(hostname);
  if (socket < 0) {
    DPRINTF("unable to connect to db\n");
    return -1;
  }

  // GET
  int read = client_get(socket, hostname, path, buffer, sizeof(buffer)-1);
  if (read <= 0) {
    DPRINTF("db returned empty response\n");
    lwip_close(socket);
    return -2;
  }

  // parse response
  int content_read_length = parse_http_response(buffer, read, &response, &content_length, output, output_length, content_type_buffer, sizeof(content_type_buffer)-1);
  if (response != 200) {
    DPRINTF("db returned %d\n", response);
    lwip_close(socket);
    return -3;
  }

  if (content_length > output_length) {
    DPRINTF("content too large %d > %d\n", content_length, output_length);
    lwip_close(socket);
    return -4;
  }

  // read rest
  total_read += content_read_length;
  output += content_read_length;
  if (callback)
    callback(total_read, content_length);
  while (total_read < content_length) {

    // 
    int left = content_length - total_read;
    if (left > 2048)
      left = 2048;
    else if (left <= 0)
      break;

    content_read_length = lwip_recv(socket, output, left, 0);
    if (content_read_length < 0) {
      DPRINTF("failed to finish reading content %d/%d (%s)\n", total_read, content_length, output);
      lwip_close(socket);
      return -5;
    }

    output += content_read_length;
    total_read += content_read_length;
    if (callback)
      callback(total_read, content_length);
    
    //DPRINTF("read %d/%d\n", total_read, content_length);
  }

  if (callback)
    callback(total_read, content_length);

  lwip_close(socket);
  return total_read;
}

int http_download(const char* hostname, const char* path, const char* file_path, downloadCallback_func callback)
{
  FILE* f = NULL;
  int response = 0, content_length = 0;
  int total_read = 0;

  DPRINTF("http_download %s -> %s\n", path, file_path);

  char* buffer = malloc(BUF_SIZE);
  if (!buffer) {
    DPRINTF("no buffer\n");
    return -1;
  }
  char* content_buffer = malloc(BUF_SIZE);
  if (!content_buffer) {
    DPRINTF("no content_buffer\n");
    return -1;
  }
  char* content_type_buffer = malloc(BUF_SIZE);
  if (!content_type_buffer) {
    DPRINTF("no content_type_buffer\n");
    return -1;
  }
  
  // connect
  int socket = client_connect(hostname);
  DPRINTF("socket %d\n", socket);
  if (socket < 0) {
    scr_printf("unable to connect to db");
    goto error;
  }

  // GET
  int read = client_get(socket, hostname, path, buffer, BUF_SIZE);
  DPRINTF("get %d\n", read);
  if (read <= 0) {
    scr_printf("no response");
    goto error;
  }

  // parse response
  int content_read_length = parse_http_response(buffer, read, &response, &content_length, content_buffer, BUF_SIZE, content_type_buffer, BUF_SIZE-1);
  if (response != 200) {
    scr_printf("%d", response);
    goto error;
  }

  // open output file
  f = fopen(file_path, "wb");
  if (!f) {
    scr_printf("usb error");
    goto error;
  }

  // write content
  fwrite(content_buffer, 1, content_read_length, f);
  total_read += content_read_length;
  if (callback) callback(total_read, content_length);
  
  // read/write rest
  while (total_read < content_length) {

    // determine buf size
    int left = content_length - total_read;
    if (left > BUF_SIZE)
      left = BUF_SIZE;
    else if (left <= 0)
      break;

    // recv
    content_read_length = lwip_recv(socket, content_buffer, left, 0);
    if (content_read_length < 0) {
      scr_printf("failed to finish reading content %d/%d (%s)", total_read, content_length, content_buffer);
      goto error;
    }

    if (content_read_length == 0)
      break;

    // write
    fwrite(content_buffer, 1, content_read_length, f);
    total_read += content_read_length;
    if (callback) callback(total_read, content_length);
  }

  // callback for 100%
  if (callback) callback(total_read, content_length);

  DPRINTF("finished %d/%d\n", total_read, content_length);
  if (buffer)
    free(buffer);
  if (content_buffer)
    free(content_buffer);
  if (content_type_buffer)
    free(content_type_buffer);
  lwip_close(socket);
  fclose(f);
  return total_read;

error: ;
  if (buffer)
    free(buffer);
  if (content_buffer)
    free(content_buffer);
  if (content_type_buffer)
    free(content_type_buffer);
  if (socket >= 0)
    lwip_close(socket);
  if (f)
    fclose(f);
  return -1;
}

int db_parse(struct DbIndex* db, int print_lines)
{
  int i;
  char content_buffer[2048];
  char url_path[512];

  // init
  db->DeltaCount = 0;
  db->ItemCount = 0;
  memset(db->Items, 0, sizeof(db->Items));

  // get index
  if (print_lines > 0) scr_printf("Getting map list...\n");
  snprintf(url_path, sizeof(url_path), INDEX_PATH, db->Path, db->GameCode, db->RegionCode);
  int read = http_get(db->Hostname, url_path, content_buffer, sizeof(content_buffer)-1, NULL);
  if (read <= 0) {
    if (print_lines > 0) scr_printf("Unable to query map index\n");
    return -1;
  }

  // parse index
  char * content = content_buffer;
  i = 0;
  while (i < MAX_DB_INDEX_ITEMS)
  {
    if (3 != sscanf(content, "%127[^\n|]|%63[^\n|]|%d\n", db->Items[i].Filename, db->Items[i].Name, &db->Items[i].RemoteVersion))
      break;
    
    content = strstr(content, "\n") + 1;
    ++i;
  }
  db->ItemCount = i;

  // parse local versions
  for (i = 0; i < db->ItemCount; ++i) {
    db_read_map_version(db, &db->Items[i]);
    if (db->Items[i].LocalVersion != db->Items[i].RemoteVersion)
      db->DeltaCount++;
  }

  // print
  if (print_lines > 0) {
    scr_printf("Total Maps: %d\n", db->ItemCount);
    scr_printf("Updates: %d\n", db->DeltaCount);
    int printed = 0;
    for (i = 0; i < db->ItemCount && printed < print_lines; ++i) {
      if (db->Items[i].LocalVersion != db->Items[i].RemoteVersion) {
        scr_printf("\t%s (version %d => %d)\n", db->Items[i].Name, db->Items[i].LocalVersion, db->Items[i].RemoteVersion);
        printed++;
      }
    }
    if (db->DeltaCount >= 10) {
      scr_printf("\t...\n");
    }
  }

  return 0;
}

int db_parse_get_count(struct DbIndex* db)
{
  int i;
  char content_buffer[2048];
  char url_path[512];

  // init
  db->DeltaCount = 0;
  db->ItemCount = 0;
  memset(db->Items, 0, sizeof(db->Items));

  // get index
  snprintf(url_path, sizeof(url_path), INDEX_PATH, db->Path, db->GameCode, db->RegionCode);
  int read = http_get(db->Hostname, url_path, content_buffer, sizeof(content_buffer)-1, NULL);
  if (read <= 0) {
    return -1;
  }

  // parse index
  char * content = content_buffer;
  i = 0;
  while (i < MAX_DB_INDEX_ITEMS)
  {
    if (3 != sscanf(content, "%127[^\n|]|%63[^\n|]|%d\n", db->Items[i].Filename, db->Items[i].Name, &db->Items[i].RemoteVersion))
      break;
    
    content = strstr(content, "\n") + 1;
    ++i;
  }
  db->ItemCount = i;

  return 0;
}

int db_check_mass(char* filepath)
{
  struct stat st = {0};
  char full_path[256];

  // check for host: first
  snprintf(full_path, sizeof(full_path), "%s:%s", DRIVE_HOST, filepath);
  if (stat(full_path, &st) == -1) {
    FILE* f = fopen(full_path, "w");
    DPRINTF("fopen %s => %d\n", full_path, f != NULL);

    // success
    if (f) {
      fclose(f);
      use_host = 1;
      scr_printf("Host drive detected\n");
      return 1;
    }
  } else {
    use_host = 1;
    scr_printf("Host drive detected\n");
    return 1;
  }

  // check for mass: last
  snprintf(full_path, sizeof(full_path), "%s:%s", DRIVE_MASS, filepath);
  if (stat(full_path, &st) == -1) {
    FILE* f = fopen(full_path, "w");
    DPRINTF("fopen %s => %d\n", full_path, f != NULL);
    if (!f) return 1;
    
    fclose(f);
  }

  scr_printf("USB drive detected\n");
  return 1;
}

int db_check_mass_dir(char* folder)
{
  struct stat st = {0};
  char dir_path[256];

  // check for host: first
  snprintf(dir_path, sizeof(dir_path), "%s:%s", DRIVE_HOST, folder);
  if (stat(dir_path, &st) == -1) {
    int r = mkdir(dir_path, 0777);
    DPRINTF("mkdir %s => %d\n", dir_path, r);

    // success
    if (r >= 0) {
      use_host = 1;
      //scr_printf("Host drive detected\n");
      return 1;
    }
  } else {
    use_host = 1;
    //scr_printf("Host drive detected\n");
    return 1;
  }

  // check for mass: last
  snprintf(dir_path, sizeof(dir_path), "%s:%s", DRIVE_MASS, folder);
  if (stat(dir_path, &st) == -1) {
    int r = mkdir(dir_path, 0777);
    DPRINTF("mkdir %s => %d\n", dir_path, r);
    if (r < 0) return 1;
  }

  //scr_printf("USB drive detected\n");
  return 1;
}

void db_download_item_callback(int bytes_downloaded, int total_bytes)
{
  float percent = (float)100.0 * (bytes_downloaded / (float)total_bytes);

  int x = scr_getX();
  int y = scr_getY();
  scr_printf("%.2f%%        ", percent);
  scr_setXY(x, y);
}

void db_write_item_callback(const char* filename, int bytes_written, int total_bytes)
{
  float percent = (float)100.0 * (bytes_written / (float)total_bytes);
  int x = scr_getX();
  int y = scr_getY();
  scr_printf("extracting %s... %.2f%%                   ", filename, percent);
  scr_setXY(x, y);
}

int extract_zip(char* zip_buffer, int zip_size, writeCallback_func callback)
{
  // parse zip
  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));

  if (!mz_zip_reader_init_mem(&zip, zip_buffer, zip_size, 0)) {
    scr_printf("failed to init zip archive\n");
    return -1;
  }

  int version_idx = -1;
  int num_files = (int)mz_zip_reader_get_num_files(&zip);
  DPRINTF("Archive has %d entries\n", num_files);

  // extract all but version files
  for (int i = 0; i < num_files+1; i++) {
    int idx = i;
    if (i == num_files) {
      if (version_idx < 0) break;
      idx = version_idx;
    }

    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, idx, &st)) {
      DPRINTF("Could not stat file %d\n", idx);
      continue;
    }

    // if found version file
    // log idx and skip
    // we want to always extract version last
    if (version_idx < 0 && strstr(st.m_filename, ".version") != NULL) {
      version_idx = idx;
      DPRINTF("Found version file %d\n", version_idx);
      continue;
    }

    DPRINTF("Extracting: %s (%u bytes)\n", st.m_filename, (unsigned)st.m_uncomp_size);

    mz_zip_reader_extract_iter_state *it = mz_zip_reader_extract_iter_new(&zip, idx, 0);
    if (!it) {
      scr_printf("failed to start extraction\n");
      continue;
    }

    // build output path
    char outPath[1024];
    snprintf(outPath, sizeof(outPath), "%s:%s", use_host ? DRIVE_HOST : DRIVE_MASS, st.m_filename);

    // open output
    FILE *out = fopen(outPath, "wb");
    if (!out) {
      scr_printf("failed to open %s\n", outPath);
      mz_zip_reader_extract_iter_free(it);
      continue;
    }

    // stream out data
    uint8_t buf[4096];
    int total = 0;
    for (;;) {
      int n = mz_zip_reader_extract_iter_read(it, buf, sizeof(buf));
      if (n < 0) {
        scr_printf("error while extracting\n");
        break;
      }
      if (n == 0) break; // end of file

      fwrite(buf, 1, n, out);
      total += n;
      if (callback) callback(st.m_filename, total, (int)st.m_uncomp_size);
    }

    fclose(out);
    mz_zip_reader_extract_iter_free(it);
    scr_printf("\n");
  }

  mz_zip_reader_end(&zip);
}

int db_download_item(struct DbIndex* db, struct DbIndexItem* item, char* buffer, int buffer_size)
{
  int ret = 0;
  char url_path[512];
  if (!item)
    return -1;

  // download zip
  snprintf(url_path, sizeof(url_path), MAP_BINARY_PATH, db->Path, db->GameCode, item->Filename, db->Ext);
  int zip_size = http_get(db->Hostname, url_path, buffer, buffer_size, &db_download_item_callback);
  scr_printf("\n");

  // extract
  if (zip_size > 0)
    ret = extract_zip(buffer, zip_size, &db_write_item_callback);
  else
    ret = -1;

  return ret;
}

int db_read_map_version(struct DbIndex* db, struct DbIndexItem* item)
{
  char file_path[512];

  // download global version
  snprintf(file_path, 511, LOCAL_BINARY_PATH, use_host ? DRIVE_HOST : DRIVE_MASS, db->GameCode, item->Filename, MAP_VERSION_EXT);

  FILE* f = fopen(file_path, "rb");
  if (!f) {
    item->LocalVersion = -1;
    DPRINTF("no version file %s\n", file_path);
    return -1;
  }
  
  fread(&item->LocalVersion, sizeof(item->LocalVersion), 1, f);
  fclose(f);
  return 0;
}

void db_ensure_trailing_slash(char* path, int path_size)
{
  if (!path || path_size <= 0) return;

  int len = strlen(path);
  if (len == 0 || len == path_size) return;

  if (path[len - 1] == '/') return;

  path[len] = '/';
  path[len+1] = 0;
}

int db_fetch_remote_repos(const char* url, struct Repo* repos, int repos_count)
{
  int i;
  char content_buffer[2048];
  char url_path[1024];
  char url_hostname[256];

  // parse host/path from url
  if (2 != sscanf(url, "%255[^/ ]%1023[^\n]", url_hostname, url_path)) {
    DPRINTF("Unable to parse repo url %s\n", url);
    return 0;
  }

  // get index
  scr_printf("Fetching horizon map repos... ");
  int read = http_get(url_hostname, url_path, content_buffer, sizeof(content_buffer)-1, NULL);
  if (read <= 0) {
    scr_printf("failed %d\n", read);
    return -1;
  }

  // parse index
  char * content = content_buffer;
  i = 0;
  while (i < repos_count)
  {
    if (4 != sscanf(content, "%255[^\n|]|%255[^/ ]%511[^\n|]|%d\n", repos[i].Name, repos[i].HostName, repos[i].Path, &repos[i].GameMask))
      break;
    
    DPRINTF("FOUND REMOTE REPO %s (%x)\n", repos[i].Name, repos[i].GameMask);
    db_ensure_trailing_slash(repos[i].Path, sizeof(repos[i].Path));
    content = strstr(content, "\n") + 1;
    ++i;
  }

  scr_printf("found %d\n", i);
  return i;
}

int db_fetch_local_repos(const char* path, struct Repo* repos, int repos_count)
{
  int i;
  char content_buffer[2048];
  char file_path[512];

  // read repo file
  scr_printf("Fetching subscribed map repos... ");
  snprintf(file_path, 511, LOCAL_PATH, use_host ? DRIVE_HOST : DRIVE_MASS, path);
  FILE* f = fopen(file_path, "rb");
  if (!f) {
    scr_printf("none\n");
    DPRINTF("unable to open %s\n", file_path);
    return 0;
  }
  
  fread(&content_buffer, sizeof(content_buffer), 1, f);
  fclose(f);

  // parse index
  char * content = content_buffer;
  i = 0;
  while (i < repos_count)
  {
    if (4 != sscanf(content, "%255[^\n|]|%255[^/ ]%511[^\n|]|%d\n", repos[i].Name, repos[i].HostName, repos[i].Path, &repos[i].GameMask))
      break;

    DPRINTF("FOUND SUBSCRIBED REPO %s (%x)\n", repos[i].Name, repos[i].GameMask);
    db_ensure_trailing_slash(repos[i].Path, sizeof(repos[i].Path));
    content = strstr(content, "\n") + 1;
    ++i;
  }

  scr_printf("found %d\n", i);
  return i;
}

int db_repo_save(const char* path, struct Repo* repos, int repos_count)
{
  int i;
  char file_path[512];

  // read repo file
  snprintf(file_path, 511, LOCAL_PATH, use_host ? DRIVE_HOST : DRIVE_MASS, path);
  FILE* f = fopen(file_path, "w");
  if (!f) {
    DPRINTF("unable to open %s\n", file_path);
    return 0;
  }
  
  for (i = 0; i < repos_count; ++i) {
    struct Repo* repo = &repos[i];
    if (!repo->GameMask) continue;

    fprintf(f, "%s|%s%s|%d\n", repo->Name, repo->HostName, repo->Path, repo->GameMask);
  }

  fclose(f);
  return 1;
}
