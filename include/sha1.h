
typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t buf[64];
} sha1_ctx;


void sha1_init(sha1_ctx *c);
void sha1_update(sha1_ctx *c, const void *data, size_t len);
void sha1_final(sha1_ctx *c, uint8_t out[20]);
void sha1(void* in, int size, uint8_t out[20]);